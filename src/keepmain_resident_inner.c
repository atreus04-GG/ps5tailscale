#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define KEEPMAIN_REASON "ps5tailscale-production-jscd"
#define KEEPMAIN_RENEW_SECONDS 30U
#define KEEPMAIN_RETRY_SECONDS 5U
#define KEEPMAIN_HEARTBEAT_SECONDS 60U
#define THREAD_NAME "tsp5_keepmain_resident"
#define LOG_PATH "/data/tailscale/keepmain-scejscd.log"
#define STATE_MAGIC_A 0x545350354b4d3031ULL
#define STATE_MAGIC_B 0x4a53434450524f44ULL
#define STATE_PHASE_INIT 0U
#define STATE_PHASE_RUNNING 1U
#define NSEC_PER_SEC 1000000000ULL

typedef void *ScePthread;
extern int scePthreadCreate(ScePthread *thr, const void *attr,
                            void *(*entry)(void *), void *arg,
                            const char *name);
extern int scePthreadDetach(ScePthread thr);
extern unsigned int sceKernelSleep(unsigned int sec);
extern int sceSystemStateMgrRequestToKeepMainOnStandby(const char *reason);

typedef struct tsp5_keepmain_state {
    uint64_t magic_a;
    uint64_t magic_b;
    uint32_t version;
    uint32_t size;
    uint32_t renew_seconds;
    uint32_t retry_seconds;
    volatile uint32_t phase;
    volatile uint32_t reserved;
    volatile uint64_t self;
    volatile uint64_t seq;
    volatile uint64_t calls;
    volatile uint64_t failed_calls;
    volatile uint64_t start_monotonic_ns;
    volatile uint64_t last_call_monotonic_ns;
    volatile uint64_t last_success_monotonic_ns;
    volatile uint64_t sleep_interruptions;
    volatile int32_t last_rc;
    volatile int32_t last_errno;
    volatile int32_t thread_create_rc;
    volatile int32_t thread_detach_rc;
} tsp5_keepmain_state_t;

volatile tsp5_keepmain_state_t g_tsp5_keepmain_state = {
    STATE_MAGIC_A,
    STATE_MAGIC_B,
    1U,
    sizeof(tsp5_keepmain_state_t),
    KEEPMAIN_RENEW_SECONDS,
    KEEPMAIN_RETRY_SECONDS,
    STATE_PHASE_INIT,
    0U,
    0ULL,
    0ULL,
    0ULL,
    0ULL,
    0ULL,
    0ULL,
    0ULL,
    0ULL,
    0x7fffffff,
    0,
    0x7fffffff,
    0x7fffffff
};

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0ULL;
    return (uint64_t)ts.tv_sec * NSEC_PER_SEC + (uint64_t)ts.tv_nsec;
}

static void append_record(const char *kind)
{
    struct timespec realtime = {0};
    struct timespec monotonic = {0};
    char line[640];
    int length;
    int fd;

    (void)clock_gettime(CLOCK_REALTIME, &realtime);
    (void)clock_gettime(CLOCK_MONOTONIC, &monotonic);
    (void)mkdir("/data/tailscale", 0777);
    length = snprintf(
        line, sizeof(line),
        "%s pid=%d realtime=%lld.%09ld monotonic=%lld.%09ld self=0x%llx phase=%u seq=%llu calls=%llu failed_calls=%llu last_rc=%d last_errno=%d last_call_ns=%llu last_success_ns=%llu sleep_interruptions=%llu create_rc=%d detach_rc=%d\n",
        kind, (int)getpid(), (long long)realtime.tv_sec, realtime.tv_nsec,
        (long long)monotonic.tv_sec, monotonic.tv_nsec,
        (unsigned long long)g_tsp5_keepmain_state.self,
        (unsigned)g_tsp5_keepmain_state.phase,
        (unsigned long long)g_tsp5_keepmain_state.seq,
        (unsigned long long)g_tsp5_keepmain_state.calls,
        (unsigned long long)g_tsp5_keepmain_state.failed_calls,
        (int)g_tsp5_keepmain_state.last_rc,
        (int)g_tsp5_keepmain_state.last_errno,
        (unsigned long long)g_tsp5_keepmain_state.last_call_monotonic_ns,
        (unsigned long long)g_tsp5_keepmain_state.last_success_monotonic_ns,
        (unsigned long long)g_tsp5_keepmain_state.sleep_interruptions,
        (int)g_tsp5_keepmain_state.thread_create_rc,
        (int)g_tsp5_keepmain_state.thread_detach_rc);
    if (length <= 0) return;
    if ((size_t)length >= sizeof(line)) length = (int)sizeof(line) - 1;

    fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_SYNC, 0666);
    if (fd < 0) return;
    (void)write(fd, line, (size_t)length);
    (void)fsync(fd);
    (void)close(fd);
}

static void *resident_thread(void *arg)
{
    uint64_t next_call;
    uint64_t next_heartbeat;
    uint64_t start;
    (void)arg;

    start = monotonic_ns();
    g_tsp5_keepmain_state.seq++;
    g_tsp5_keepmain_state.start_monotonic_ns = start;
    g_tsp5_keepmain_state.phase = STATE_PHASE_RUNNING;
    g_tsp5_keepmain_state.seq++;
    next_call = 0ULL;
    next_heartbeat = start + (uint64_t)KEEPMAIN_HEARTBEAT_SECONDS * NSEC_PER_SEC;
    append_record("KEEPMAIN_SCEJSCD_THREAD_START");

    for (;;) {
        uint64_t now = monotonic_ns();
        unsigned int retry_seconds;

        if (now != 0ULL && (next_call == 0ULL || now >= next_call)) {
            g_tsp5_keepmain_state.seq++;
            errno = 0;
            g_tsp5_keepmain_state.last_rc =
                sceSystemStateMgrRequestToKeepMainOnStandby(KEEPMAIN_REASON);
            g_tsp5_keepmain_state.last_errno = errno;
            g_tsp5_keepmain_state.calls++;
            g_tsp5_keepmain_state.last_call_monotonic_ns = now;
            if (g_tsp5_keepmain_state.last_rc == 0) {
                g_tsp5_keepmain_state.last_success_monotonic_ns = now;
                retry_seconds = KEEPMAIN_RENEW_SECONDS;
            } else {
                g_tsp5_keepmain_state.failed_calls++;
                retry_seconds = KEEPMAIN_RETRY_SECONDS;
            }
            next_call = now + (uint64_t)retry_seconds * NSEC_PER_SEC;
            g_tsp5_keepmain_state.seq++;
            if (g_tsp5_keepmain_state.last_rc != 0) {
                append_record("KEEPMAIN_SCEJSCD_RENEW_FAILED");
            }
        }
        if (now != 0ULL && now >= next_heartbeat) {
            append_record("KEEPMAIN_SCEJSCD_HEARTBEAT");
            next_heartbeat = now +
                (uint64_t)KEEPMAIN_HEARTBEAT_SECONDS * NSEC_PER_SEC;
        }
        if (sceKernelSleep(1) != 0U) {
            g_tsp5_keepmain_state.sleep_interruptions++;
        }
    }
}

int main(void)
{
    ScePthread thread = NULL;
    int rc;

    g_tsp5_keepmain_state.self = (uint64_t)(uintptr_t)&g_tsp5_keepmain_state;
    append_record("KEEPMAIN_SCEJSCD_INJECTED");
    rc = scePthreadCreate(&thread, NULL, resident_thread, NULL, THREAD_NAME);
    g_tsp5_keepmain_state.thread_create_rc = rc;
    if (rc != 0) {
        append_record("KEEPMAIN_SCEJSCD_THREAD_CREATE_FAILED");
        return 2;
    }
    rc = scePthreadDetach(thread);
    g_tsp5_keepmain_state.thread_detach_rc = rc;
    if (rc != 0) {
        append_record("KEEPMAIN_SCEJSCD_THREAD_DETACH_FAILED");
    }
    return rc == 0 ? 0 : 3;
}
