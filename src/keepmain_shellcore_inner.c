#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CONTROL_PATH "/data/tailscale/keepmain.control"
#define LOG_PATH "/data/tailscale/keepmain-shellcore-resident.log"
#define READY_PATH "/data/tailscale/keepmain-shellcore-resident.ready"
#define KEEPMAIN_REASON "ps5tailscale-shellcore-companion"
#define KEEPMAIN_RENEW_SECONDS 30LL
#define KEEPMAIN_RETRY_SECONDS 5LL
#define HEARTBEAT_SECONDS 30LL

typedef void *ScePthread;
extern int scePthreadCreate(ScePthread *thr, const void *attr,
                            void *(*entry)(void *), void *arg,
                            const char *name);
extern int scePthreadDetach(ScePthread thr);
extern unsigned int sceKernelSleep(unsigned int sec);
extern int sceSystemStateMgrRequestToKeepMainOnStandby(const char *reason);

static void append_record(const char *kind, int rc, int err)
{
    struct timespec realtime = {0};
    struct timespec monotonic = {0};
    char line[512];
    int length;
    int fd;

    (void)clock_gettime(CLOCK_REALTIME, &realtime);
    (void)clock_gettime(CLOCK_MONOTONIC, &monotonic);
    (void)mkdir("/data/tailscale", 0777);
    length = snprintf(
        line, sizeof(line),
        "%s pid=%d realtime=%lld.%09ld monotonic=%lld.%09ld rc=%d rc_hex=0x%08x errno=%d\n",
        kind, (int)getpid(), (long long)realtime.tv_sec, realtime.tv_nsec,
        (long long)monotonic.tv_sec, monotonic.tv_nsec,
        rc, (unsigned)rc, err);
    if (length <= 0) return;
    if ((size_t)length >= sizeof(line)) length = (int)sizeof(line) - 1;

    fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_SYNC, 0666);
    if (fd < 0) return;
    (void)write(fd, line, (size_t)length);
    (void)fsync(fd);
    (void)close(fd);
}

static int control_is_active(void)
{
    char value = '0';
    int fd = open(CONTROL_PATH, O_RDONLY);
    ssize_t n;

    if (fd < 0) return 0;
    n = read(fd, &value, 1);
    (void)close(fd);
    return n == 1 && value == '1';
}

static void publish_ready(void)
{
    int fd = open(READY_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;
    (void)write(fd, "ready\n", 6);
    (void)fsync(fd);
    (void)close(fd);
}

static void *resident_thread(void *arg)
{
    long long next_attempt = -1;
    long long next_heartbeat = -1;
    int was_active = -1;
    (void)arg;

    append_record("KEEPMAIN_RESIDENT_THREAD_START", 0, 0);
    publish_ready();
    for (;;) {
        struct timespec now = {0};
        int active = control_is_active();

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            append_record("KEEPMAIN_CLOCK_FAILED", -1, errno);
            (void)sceKernelSleep(1);
            continue;
        }

        if (active != was_active) {
            append_record(active ? "KEEPMAIN_CONTROL_ACTIVE" : "KEEPMAIN_CONTROL_INACTIVE", 0, 0);
            was_active = active;
            next_attempt = active ? 0 : -1;
        }

        if (active && (next_attempt < 0 || (long long)now.tv_sec >= next_attempt)) {
            int rc;
            int saved_errno;
            errno = 0;
            rc = sceSystemStateMgrRequestToKeepMainOnStandby(KEEPMAIN_REASON);
            saved_errno = errno;
            append_record("KEEPMAIN_RENEW_RETURN", rc, saved_errno);
            next_attempt = (long long)now.tv_sec +
                (rc == 0 ? KEEPMAIN_RENEW_SECONDS : KEEPMAIN_RETRY_SECONDS);
        }

        if (next_heartbeat < 0 || (long long)now.tv_sec >= next_heartbeat) {
            append_record(active ? "KEEPMAIN_RESIDENT_HEARTBEAT_ACTIVE" :
                                   "KEEPMAIN_RESIDENT_HEARTBEAT_INACTIVE", 0, 0);
            next_heartbeat = (long long)now.tv_sec + HEARTBEAT_SECONDS;
        }
        (void)sceKernelSleep(1);
    }
}

int main(void)
{
    ScePthread thread = NULL;
    int rc;

    append_record("KEEPMAIN_RESIDENT_INJECTED", 0, 0);
    rc = scePthreadCreate(&thread, NULL, resident_thread, NULL,
                          "tsp5_keepmain_shellcore");
    append_record("KEEPMAIN_RESIDENT_THREAD_CREATE", rc, 0);
    if (rc != 0) return 2;
    rc = scePthreadDetach(thread);
    append_record("KEEPMAIN_RESIDENT_THREAD_DETACH", rc, 0);
    return rc == 0 ? 0 : 3;
}
