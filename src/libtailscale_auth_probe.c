#include "ps5_log.h"
#ifndef TSP5_EXTERNAL_LAUNCHER_BOOTSTRAP
#include "media_launcher.h"
#endif
#include "dashboard.h"
#include "tailscale.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/procctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/user.h>
#include <netinet/in.h>
#include <poll.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>

typedef struct notify_request {
    char reserved[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
int TsnetPS5SetDisableListenerCloseWatch(int enabled);
int TsnetPS5SetForceNoiseIPv4(int enabled);
int TsnetPS5SetTraceRawControl(int enabled);
int TsnetPS5SetPollTraceFD(int fd);
int TsnetPS5StartTimerHeartbeat(void);
#if !defined(TSP5_KEEPMAIN_EXTERNAL_TREATMENT) || defined(TSP5_KEEPMAIN_LIFECYCLE_CONTROL)
#define KEEPMAIN_CONTROL_PATH "/data/tailscale/keepmain.control"
#define KEEPMAIN_CONTROL_TMP "/data/tailscale/keepmain.control.tmp"
#endif

static void notify(const char *message)
{
    notify_request_t request;
    memset(&request, 0, sizeof(request));
    (void)snprintf(request.message, sizeof(request.message), "%s", message);
    (void)sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

static void native_delay_ms(long long ms)
{
    struct timespec req;
    struct timespec rem;

    req.tv_sec = (time_t)(ms / 1000LL);
    req.tv_nsec = (long)((ms % 1000LL) * 1000000LL);
    while (nanosleep(&req, &rem) != 0 && errno == EINTR) {
        req = rem;
    }
}

#if !defined(TSP5_KEEPMAIN_EXTERNAL_TREATMENT) || defined(TSP5_KEEPMAIN_LIFECYCLE_CONTROL)
static int keepmain_control_write(int active)
{
    FILE *f = fopen(KEEPMAIN_CONTROL_TMP, "wb");
    if (f == NULL) return -1;
    if (fputc(active ? '1' : '0', f) == EOF || fputc('\n', f) == EOF) {
        int saved = errno;
        (void)fclose(f);
        (void)unlink(KEEPMAIN_CONTROL_TMP);
        errno = saved ? saved : EIO;
        return -1;
    }
    if (fclose(f) != 0) {
        (void)unlink(KEEPMAIN_CONTROL_TMP);
        return -1;
    }
    if (rename(KEEPMAIN_CONTROL_TMP, KEEPMAIN_CONTROL_PATH) != 0) {
        (void)unlink(KEEPMAIN_CONTROL_TMP);
        return -1;
    }
    return 0;
}
#endif

static void keepmain_set_active(int active, int *last_state)
{
    if (*last_state == active) return;
#ifdef TSP5_KEEPMAIN_LIFECYCLE_CONTROL
    if (keepmain_control_write(active) != 0) {
        ps5_log("ERROR", "KEEPMAIN_CONTROL_WRITE active=%d errno=%d %s",
                active, errno, strerror(errno));
        return;
    }
    *last_state = active;
    ps5_log("INFO", "KEEPMAIN_CONTROL active=%d companion=SceShellCore external=SceJSCd", active);
#elif defined(TSP5_KEEPMAIN_EXTERNAL_TREATMENT)
    *last_state = active;
    ps5_log("INFO", "KEEPMAIN_EXTERNAL_RESIDENT desired_active=%d host=SceJSCd lifetime=until-reboot",
            active);
#else
    if (keepmain_control_write(active) != 0) {
        ps5_log("ERROR", "KEEPMAIN_CONTROL_WRITE active=%d errno=%d %s",
                active, errno, strerror(errno));
        return;
    }
    *last_state = active;
    ps5_log("INFO", "KEEPMAIN_CONTROL active=%d", active);
#endif
}

static int read_self_process_state(int *state, unsigned *xstat, int *tracer,
                                   int *threads)
{
    unsigned char buf[2048];
    const int pid = (int)getpid();
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    struct kinfo_proc *kp;
    size_t len = sizeof(buf);

    memset(buf, 0, sizeof(buf));
    if (sysctl(mib, 4, buf, &len, NULL, 0) != 0 ||
            len < sizeof(struct kinfo_proc)) {
        return -1;
    }
    kp = (struct kinfo_proc *)buf;
    *state = (int)kp->ki_stat;
    *xstat = (unsigned)kp->ki_xstat;
    *tracer = (int)kp->ki_tracer;
    *threads = (int)kp->ki_numthreads;
    return 0;
}

static void *auth151_native_heartbeat_main(void *unused)
{
    char line[128];
    int fd;
    int seq;
    int line_len;
    (void)unused;

    fd = ps5_log_remote_dup();
    if (fd < 0) {
        ps5_log("ERROR", "AUTH169_DIRECT_HEARTBEAT socket open failed");
        return NULL;
    }
    line_len = snprintf(line, sizeof(line),
                        "[AUTH169 direct-heartbeat] start fd=%d\n", fd);
    if (line_len > 0) {
        (void)send(fd, line, (size_t)line_len, 0);
    }
    for (seq = 1; seq <= 90; seq++) {
        int state = -1;
        unsigned xstat = 0;
        int tracer = -1;
        int threads = -1;
        int state_rc;

        native_delay_ms(1000);
        state_rc = read_self_process_state(&state, &xstat, &tracer, &threads);
        line_len = snprintf(line, sizeof(line),
                            "[AUTH169 direct-heartbeat] seq=%d state_rc=%d stat=%d xstat=%u tracer=%d threads=%d\n",
                            seq, state_rc, state, xstat, tracer, threads);
        if (line_len > 0) {
            (void)send(fd, line, (size_t)line_len, 0);
        }
    }
    (void)close(fd);
    return NULL;
}

static int auth151_native_timeout_probe(void)
{
    int fd;
    int rc;
    char b = 0;
    struct timeval tv;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        ps5_log("ERROR", "AUTH151_NATIVE_TIMEOUT socket failed errno=%d", errno);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ps5_log("ERROR", "AUTH151_NATIVE_TIMEOUT bind failed errno=%d", errno);
        (void)close(fd);
        return -1;
    }

    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        ps5_log("ERROR", "AUTH151_NATIVE_TIMEOUT setsockopt failed errno=%d", errno);
        (void)close(fd);
        return -1;
    }

    {
        int recv_errno;
        errno = 0;
        rc = (int)recv(fd, &b, 1, 0);
        recv_errno = errno;
        ps5_log("INFO", "AUTH151_NATIVE_TIMEOUT recv rc=%d errno=%d", rc, recv_errno);
        (void)close(fd);
        return (rc < 0 && (recv_errno == EAGAIN || recv_errno == EWOULDBLOCK)) ? 0 : -1;
    }
}

static atomic_int start_watchdog_armed;
static atomic_int auth_watchdog_armed;
static atomic_int listener_watchdog_armed;
static atomic_int close_watchdog_armed;

#ifndef TSP5_EXTERNAL_LAUNCHER_BOOTSTRAP
static void *media_launcher_install_main(void *unused)
{
    (void)unused;
    ps5_log("INFO", "MEDIA_LAUNCHER_SETUP_BEGIN");
    if (tsp5_media_launcher_install_if_needed() != 0) {
        ps5_log("ERROR", "MEDIA_LAUNCHER_SETUP_FAILED continuing_without_launcher");
    } else {
        ps5_log("INFO", "MEDIA_LAUNCHER_SETUP_DONE");
    }
    return NULL;
}
#endif

static void *start_watchdog_main(void *unused)
{
    (void)unused;
    native_delay_ms(20000);
    if (atomic_load(&start_watchdog_armed) != 0) {
        ps5_log("ERROR", "AUTH_START_WATCHDOG timeout pid=%d; terminating payload",
                (int)getpid());
        _exit(124);
    }
    return NULL;
}

static void *auth_watchdog_main(void *unused)
{
    (void)unused;
    native_delay_ms(150000);
    if (atomic_load(&auth_watchdog_armed) != 0) {
        ps5_log("ERROR", "AUTH_WAIT_WATCHDOG timeout pid=%d; terminating payload",
                (int)getpid());
        _exit(126);
    }
    return NULL;
}

static void *close_watchdog_main(void *unused)
{
    (void)unused;
    native_delay_ms(20000);
    if (atomic_load(&close_watchdog_armed) != 0) {
        ps5_log("ERROR", "AUTH_CLOSE_WATCHDOG timeout pid=%d; terminating payload",
                (int)getpid());
        _exit(125);
    }
    return NULL;
}

static void *listener_watchdog_main(void *unused)
{
    (void)unused;
    native_delay_ms(30000);
    if (atomic_load(&listener_watchdog_armed) != 0) {
        ps5_log("ERROR", "AUTH197_LISTEN_WATCHDOG timeout pid=%d; terminating payload",
                (int)getpid());
        _exit(127);
    }
    return NULL;
}

void tsp5_go_lib_stage(int stage)
{
    ps5_log("INFO", "Go libtailscale stage %d", stage);
}

static void log_ts_error(tailscale sd, const char *operation)
{
    char message[1024];
    int rc = tailscale_errmsg(sd, message, sizeof(message));
    if (rc == 0) {
        ps5_log("ERROR", "%s failed: %s", operation, message);
    } else {
        ps5_log("ERROR", "%s failed; errmsg rc=%d", operation, rc);
    }
}

static int json_string_field(const char *json, const char *field,
                             char *out, size_t out_size)
{
    char needle[96];
    const char *p;
    const char *end;
    size_t n;

    if (snprintf(needle, sizeof(needle), "\"%s\":\"", field) < 0) return -1;
    p = strstr(json, needle);
    if (p == NULL) return -1;
    p += strlen(needle);
    end = strchr(p, '"');
    if (end == NULL) return -1;
    n = (size_t)(end - p);
    if (n >= out_size) n = out_size - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

static int status_is_running(const char *json)
{
    return strstr(json, "\"BackendState\":\"Running\"") != NULL;
}

static void auth175_adjust_cpu_limit(void)
{
    struct rlimit before;
    struct rlimit after;

    errno = 0;
    if (getrlimit(RLIMIT_CPU, &before) != 0) {
        ps5_log("ERROR", "AUTH175_RLIMIT_CPU get-before failed errno=%d %s",
                errno, strerror(errno));
        return;
    }

    ps5_log("INFO", "AUTH175_RLIMIT_CPU before soft=%llu hard=%llu",
            (unsigned long long)before.rlim_cur,
            (unsigned long long)before.rlim_max);
    if (before.rlim_cur == RLIM_INFINITY) {
        ps5_log("INFO", "AUTH175_RLIMIT_CPU unchanged reason=soft-unlimited");
        return;
    }
    if (before.rlim_max <= before.rlim_cur) {
        ps5_log("INFO", "AUTH175_RLIMIT_CPU unchanged reason=hard-not-higher");
        return;
    }

    after = before;
    after.rlim_cur = before.rlim_max;
    errno = 0;
    if (setrlimit(RLIMIT_CPU, &after) != 0) {
        ps5_log("ERROR", "AUTH175_RLIMIT_CPU set failed errno=%d %s",
                errno, strerror(errno));
        return;
    }

    errno = 0;
    if (getrlimit(RLIMIT_CPU, &after) != 0) {
        ps5_log("ERROR", "AUTH175_RLIMIT_CPU get-after failed errno=%d %s",
                errno, strerror(errno));
        return;
    }
    ps5_log("INFO", "AUTH175_RLIMIT_CPU after soft=%llu hard=%llu",
            (unsigned long long)after.rlim_cur,
            (unsigned long long)after.rlim_max);
}

static void auth179_disable_tracing(void)
{
    int status = -1;
    int control = PROC_TRACE_CTL_DISABLE;
    int rc;

    errno = 0;
    rc = procctl(P_PID, (id_t)getpid(), PROC_TRACE_STATUS, &status);
    if (rc != 0) {
        ps5_log("ERROR", "AUTH179_TRACE_STATUS before failed rc=%d kernel_errno=%d libc_errno=%d %s",
                rc, rc > 0 ? rc : -rc, errno, strerror(errno));
    } else {
        ps5_log("INFO", "AUTH179_TRACE_STATUS before=%d", status);
    }

    errno = 0;
    rc = procctl(P_PID, (id_t)getpid(), PROC_TRACE_CTL, &control);
    if (rc != 0) {
        ps5_log("ERROR", "AUTH179_TRACE_CTL disable failed rc=%d kernel_errno=%d libc_errno=%d %s",
                rc, rc > 0 ? rc : -rc, errno, strerror(errno));
        return;
    }
    ps5_log("INFO", "AUTH179_TRACE_CTL disable succeeded");

    status = -1;
    errno = 0;
    rc = procctl(P_PID, (id_t)getpid(), PROC_TRACE_STATUS, &status);
    if (rc != 0) {
        ps5_log("ERROR", "AUTH179_TRACE_STATUS after failed rc=%d kernel_errno=%d libc_errno=%d %s",
                rc, rc > 0 ? rc : -rc, errno, strerror(errno));
    } else {
        ps5_log("INFO", "AUTH179_TRACE_STATUS after=%d", status);
    }
}

int main(void)
{
    int result = 0;
    int tslog_fd = -1;
    int poll_trace_fd = -1;
    tailscale sd = -1;
    tailscale_listener listener = -1;
    tailscale_conn inbound_conn = -1;
    pthread_t start_watchdog_thread;
    pthread_t auth_watchdog_thread;
    pthread_t listener_watchdog_thread;
    pthread_t close_watchdog_thread;
    pthread_t auth151_native_heartbeat_thread;
#ifndef TSP5_EXTERNAL_LAUNCHER_BOOTSTRAP
    pthread_t media_launcher_thread;
#endif
    int watchdog_rc;
    int rc;
    int elapsed;
    int running = 0;
    int tailnet_connected = 0;
    int reconnect_pending = 0;
    int accepted_connections = 0;
    int keepmain_control_state = -1;
    char last_auth_url[1024] = "";
    char auth_url[1024];
    char ips[256];
    char remoteplay_target_ip[64] = "";

    (void)ps5_log_init();
    ps5_log("INFO", "DASHBOARD_BUILD_START pid=%d", (int)getpid());
    if (tsp5_dashboard_start() != 0) {
        ps5_log("ERROR", "DASHBOARD_SETUP_FAILED continuing_without_dashboard");
    }
#ifndef TSP5_EXTERNAL_LAUNCHER_BOOTSTRAP
    watchdog_rc = pthread_create(&media_launcher_thread, NULL,
                                 media_launcher_install_main, NULL);
    if (watchdog_rc == 0) {
        (void)pthread_detach(media_launcher_thread);
        ps5_log("INFO", "MEDIA_LAUNCHER_SETUP_THREAD_STARTED");
    } else {
        ps5_log("ERROR", "MEDIA_LAUNCHER_THREAD_FAILED rc=%d", watchdog_rc);
    }
#else
    ps5_log("INFO", "MEDIA_LAUNCHER_EXTERNAL_BOOTSTRAP active");
#endif
    ps5_log("INFO", "AUTH179 disabling future tracing before Go/Tailscale startup");
    auth179_disable_tracing();
    ps5_log("INFO", "AUTH175 checking inherited CPU-time limit before Go/Tailscale startup");
    auth175_adjust_cpu_limit();
    {
        int state = -1;
        unsigned xstat = 0;
        int tracer = -1;
        int threads = -1;
        int state_rc = read_self_process_state(&state, &xstat, &tracer,
                                               &threads);
        ps5_log("INFO", "AUTH177_TRACER_STATE startup rc=%d stat=%d xstat=%u tracer=%d threads=%d",
                state_rc, state, xstat, tracer, threads);
    }
    ps5_log("INFO", "AUTH174 control EAGAIN uses 1ms native backoff; AUTH171 async preemption remains disabled");
    ps5_log("INFO", "AUTH181 Go syscall entry points deny SYS_ptrace=26 with EPERM");
    ps5_log("INFO", "AUTH183 Go signal handler logs signal number and si_code at entry");
    ps5_log("INFO", "AUTH186 tracing signal-send syscalls kill=37 thr_kill=433 sigqueue=456 thr_kill2=481 pdkill=519");
    ps5_log("INFO", "AUTH190 syscall assembly scratch frames declare NO_LOCAL_POINTERS");
    ps5_log("INFO", "AUTH191 libtailscale startup ignores SIGPIPE before tsnet.Server.Start");
    ps5_log("INFO", "AUTH192 restoring engine.Reconfig side effects");
    ps5_log("INFO", "AUTH193 restoring ReSTUN endpoint discovery");
    ps5_log("INFO", "AUTH197 promoting the proven poll-gated listener to resident service; peer API and async RequestStatus remain disabled");
    ps5_log("INFO", "libtailscale resident service starting build=AUTH197-RESIDENT-LISTENER pid=%d ppid=%d",
            (int)getpid(), (int)getppid());
    notify("tailscale-ps5: AUTH175 CPU-limit diagnostic starting");

    rc = auth151_native_timeout_probe();
    ps5_log("INFO", "AUTH151_NATIVE_TIMEOUT_RESULT rc=%d", rc);

    watchdog_rc = pthread_create(&auth151_native_heartbeat_thread, NULL,
                                 auth151_native_heartbeat_main, NULL);
    if (watchdog_rc == 0) {
        (void)pthread_detach(auth151_native_heartbeat_thread);
        ps5_log("INFO", "AUTH151_NATIVE_HEARTBEAT_START rc=0");
    } else {
        ps5_log("ERROR", "AUTH151_NATIVE_HEARTBEAT_START rc=%d", watchdog_rc);
    }

    if (TsnetPS5SetDisableListenerCloseWatch(1) != 0) {
        ps5_log("ERROR", "TsnetPS5SetDisableListenerCloseWatch failed");
        result = 1;
        goto done;
    }
    ps5_log("INFO", "AUTH197 listener close-watch disabled; resident poll-gated listener will be created");
    if (TsnetPS5SetForceNoiseIPv4(1) != 0) {
        ps5_log("ERROR", "TsnetPS5SetForceNoiseIPv4 failed");
        result = 1;
        goto done;
    }
    if (TsnetPS5SetTraceRawControl(1) != 0) {
        ps5_log("ERROR", "TsnetPS5SetTraceRawControl failed");
        result = 1;
        goto done;
    }
    ps5_log("INFO", "AUTH193-CONTROL-READ-BREAK reusing AUTH133 fresh profile; ReSTUN restored");

    if (mkdir("/data/tailscale/libtailscale-auth133-fresh", 0777) != 0 && errno != EEXIST) {
        ps5_log("ERROR", "mkdir state dir failed: %s", strerror(errno));
        result = 1;
        goto done;
    }

    tslog_fd = ps5_log_remote_dup();
    if (tslog_fd < 0) {
        ps5_log("ERROR", "cannot open remote tsnet log socket");
        result = 1;
        goto done;
    }

    poll_trace_fd = ps5_log_remote_dup();
    if (poll_trace_fd < 0) {
        ps5_log("ERROR", "AUTH151 direct poll trace socket open failed");
        result = 1;
        goto done;
    }
    if (TsnetPS5SetPollTraceFD(poll_trace_fd) != 0) {
        ps5_log("ERROR", "AUTH151 direct poll trace fd registration failed");
        result = 1;
        goto done;
    }
    ps5_log("INFO", "AUTH151_DIRECT_POLL_TRACE_FD fd=%d", poll_trace_fd);

    sd = tailscale_new();
    if (sd < 0) {
        ps5_log("ERROR", "tailscale_new failed handle=%d", sd);
        result = 1;
        goto done;
    }
    if (tailscale_set_dir(sd, "/data/tailscale/libtailscale-auth133-fresh") != 0 ||
        tailscale_set_hostname(sd, "ps5-tailscale") != 0 ||
        tailscale_set_logfd(sd, tslog_fd) != 0) {
        log_ts_error(sd, "tailscale configuration");
        result = 1;
        goto close_server;
    }

    {
        int identity_present = tsp5_dashboard_identity_present();
        char auth_key[512];
        FILE *keyf;
        size_t key_len;

        if (identity_present) {
            ps5_log("INFO", "ENROLLMENT existing profile detected; retained auth key ignored");
        } else {
            ps5_log("INFO", "ENROLLMENT no persisted profile; waiting for dashboard registration request");
            notify("tailscale-ps5: open Media > Tailscale to register");
            if (tsp5_dashboard_wait_for_registration() != 0) {
                ps5_log("ERROR", "ENROLLMENT dashboard registration wait failed");
                result = 1;
                goto close_server;
            }

            memset(auth_key, 0, sizeof(auth_key));
            keyf = fopen("/data/tailscale/ps5-auth.key", "rb");
            if (keyf == NULL) {
                ps5_log("ERROR", "ENROLLMENT registration requested but auth key file is missing");
                tsp5_dashboard_set_error("Authentication key file is missing.");
                result = 1;
                goto close_server;
            }
            if (fgets(auth_key, sizeof(auth_key), keyf) == NULL) {
                ps5_log("ERROR", "ENROLLMENT auth key file read failed");
                (void)fclose(keyf);
                tsp5_dashboard_set_error("Authentication key could not be read.");
                result = 1;
                goto close_server;
            }
            (void)fclose(keyf);

            key_len = strcspn(auth_key, "\r\n");
            auth_key[key_len] = '\0';
            if (strncmp(auth_key, "tskey-", 6) != 0) {
                ps5_log("ERROR", "ENROLLMENT auth key has invalid format");
                memset(auth_key, 0, sizeof(auth_key));
                tsp5_dashboard_set_error("Invalid Tailscale auth key.");
                result = 1;
                goto close_server;
            }
            if (tailscale_set_authkey(sd, auth_key) != 0) {
                memset(auth_key, 0, sizeof(auth_key));
                log_ts_error(sd, "tailscale_set_authkey");
                tsp5_dashboard_set_error("Tailscale rejected the auth key.");
                result = 1;
                goto close_server;
            }
            memset(auth_key, 0, sizeof(auth_key));
            ps5_log("INFO", "ENROLLMENT auth key loaded; file retained; buffer zeroed");
        }
    }

    tsp5_dashboard_set_connecting();

    atomic_store(&start_watchdog_armed, 1);
    watchdog_rc = pthread_create(&start_watchdog_thread, NULL,
                                 start_watchdog_main, NULL);
    if (watchdog_rc == 0) {
        (void)pthread_detach(start_watchdog_thread);
        ps5_log("INFO", "AUTH_START_WATCHDOG armed timeout=20s");
    } else {
        atomic_store(&start_watchdog_armed, 0);
        ps5_log("ERROR", "AUTH_START_WATCHDOG pthread_create failed rc=%d", watchdog_rc);
    }

    ps5_log("INFO", "tailscale_start starting");
    rc = tailscale_start(sd);
    atomic_store(&start_watchdog_armed, 0);
    ps5_log("INFO", "AUTH_START_WATCHDOG disarmed after tailscale_start return");
    if (rc != 0) {
        log_ts_error(sd, "tailscale_start");
        result = 1;
        goto close_server;
    }
    ps5_log("INFO", "tailscale_start passed; AUTH170 artificial Go timer heartbeat intentionally skipped");
    ps5_log("INFO", "AUTH170_GO_TIMER_HEARTBEAT_SKIPPED");
    ps5_log("INFO", "AUTH150_POST_START_WINDOW entered");
    notify("tailscale-ps5: waiting for login");

    atomic_store(&auth_watchdog_armed, 1);
    watchdog_rc = pthread_create(&auth_watchdog_thread, NULL,
                                 auth_watchdog_main, NULL);
    if (watchdog_rc == 0) {
        (void)pthread_detach(auth_watchdog_thread);
        ps5_log("INFO", "AUTH_WAIT_WATCHDOG armed timeout=150s");
    } else {
        atomic_store(&auth_watchdog_armed, 0);
        ps5_log("ERROR", "AUTH_WAIT_WATCHDOG pthread_create failed rc=%d", watchdog_rc);
    }

    for (elapsed = 0; elapsed <= 120; elapsed += 2) {
        char *status_json = NULL;

        ps5_log("INFO", "AUTH150_STATUS before elapsed=%d", elapsed);
        rc = tailscale_status_json(sd, &status_json);
        ps5_log("INFO", "AUTH150_STATUS after elapsed=%d rc=%d", elapsed, rc);
        if (rc == 0 && status_json != NULL) {
            auth_url[0] = '\0';
            if (json_string_field(status_json, "AuthURL",
                                  auth_url, sizeof(auth_url)) == 0 &&
                auth_url[0] != '\0' &&
                strcmp(auth_url, last_auth_url) != 0) {
                (void)snprintf(last_auth_url, sizeof(last_auth_url),
                               "%s", auth_url);
                ps5_log("INFO", "AUTH_URL=%s", auth_url);
                notify(auth_url);
            }

            if (status_is_running(status_json)) {
                running = 1;
                free(status_json);
                status_json = NULL;
                break;
            }
            free(status_json);
        } else if (rc != 0) {
            log_ts_error(sd, "tailscale_status_json");
        }
        ps5_log("INFO", "AUTH150_DELAY before elapsed=%d", elapsed);
        native_delay_ms(2000);
        ps5_log("INFO", "AUTH150_DELAY after elapsed=%d", elapsed);
    }

    atomic_store(&auth_watchdog_armed, 0);
    ps5_log("INFO", "AUTH_WAIT_WATCHDOG disarmed after auth wait");

    if (!running) {
        ps5_log("ERROR", "AUTH_TIMEOUT no Running state after 120s");
        result = 2;
        goto close_server;
    }

    ips[0] = '\0';
    rc = tailscale_getips(sd, ips, sizeof(ips));
    if (rc != 0) {
        log_ts_error(sd, "tailscale_getips");
        result = 1;
        goto close_server;
    }
    ps5_log("INFO", "TAILNET_IPS=%s", ips);
    if (tsp5_dashboard_mark_registered() != 0) {
        ps5_log("ERROR", "ENROLLMENT connected but registered marker could not be written");
    }
    tsp5_dashboard_set_connected(ips);
    tailnet_connected = 1;
    keepmain_set_active(1, &keepmain_control_state);
    notify("tailscale-ps5: tailnet Running");

    if (tsp5_dashboard_get_lan_ipv4(remoteplay_target_ip, sizeof(remoteplay_target_ip)) == 0 &&
        remoteplay_target_ip[0] != '\0') {
        rc = tailscale_ps5_start_remoteplay_proxy(sd, remoteplay_target_ip);
        if (rc != 0) {
            log_ts_error(sd, "REMOTEPLAY_PROXY start");
            ps5_log("ERROR", "REMOTEPLAY_PROXY_START_FAILED target=%s", remoteplay_target_ip);
        } else {
            ps5_log("INFO", "REMOTEPLAY_PROXY_READY target=%s tcp=9295 udp=9295,9296,9297,9302",
                    remoteplay_target_ip);
        }
    } else {
        ps5_log("ERROR", "REMOTEPLAY_PROXY_START_SKIPPED reason=no_lan_ipv4");
    }

    ps5_log("INFO", "AUTH197_OUTBOUND_DIAL_SKIPPED preserving inbound-only resident service");
    atomic_store(&listener_watchdog_armed, 1);
    watchdog_rc = pthread_create(&listener_watchdog_thread, NULL,
                                  listener_watchdog_main, NULL);
    if (watchdog_rc != 0) {
        atomic_store(&listener_watchdog_armed, 0);
        ps5_log("ERROR", "AUTH197_LISTEN_WATCHDOG pthread_create failed rc=%d",
                watchdog_rc);
        result = 1;
        goto close_server;
    }
    (void)pthread_detach(listener_watchdog_thread);
    ps5_log("INFO", "AUTH197_LISTEN_WATCHDOG armed timeout=30s");
    rc = tailscale_listen(sd, "tcp", ":8085", &listener);
    atomic_store(&listener_watchdog_armed, 0);
    if (rc != 0) {
        log_ts_error(sd, "AUTH197 tailscale_listen");
        result = 1;
        goto close_server;
    }
    ps5_log("INFO", "TAILNET_LISTENER_READY port=8085 fd=%d", listener);
    ps5_log("INFO", "AUTH197_RESIDENT_SERVICE_READY port=8085 poll-gated=true lifetime=until-reboot");

    for (elapsed = 0;;) {
        struct pollfd pfd;
        int lifecycle_cmd = tsp5_dashboard_take_lifecycle_command();

        if (lifecycle_cmd == 1) {
            rc = tailscale_set_running(sd, 0);
            if (rc != 0) {
                log_ts_error(sd, "LIFECYCLE disconnect");
                tsp5_dashboard_set_error("Disconnect failed.");
            } else {
                tailnet_connected = 0;
                reconnect_pending = 0;
                keepmain_set_active(0, &keepmain_control_state);
                tsp5_dashboard_set_disconnected();
                ps5_log("INFO", "LIFECYCLE_DISCONNECTED wantRunning=false");
            }
        } else if (lifecycle_cmd == 2) {
            rc = tailscale_set_running(sd, 1);
            if (rc != 0) {
                log_ts_error(sd, "LIFECYCLE reconnect");
                tsp5_dashboard_set_error("Reconnect request failed.");
            } else {
                reconnect_pending = 1;
                keepmain_set_active(1, &keepmain_control_state);
                tsp5_dashboard_set_connecting();
                ps5_log("INFO", "LIFECYCLE_RECONNECT_REQUESTED wantRunning=true");
            }
        }

        if (reconnect_pending) {
            char *status_json = NULL;
            if (tailscale_status_json(sd, &status_json) == 0 && status_json != NULL) {
                if (strstr(status_json, "\"BackendState\":\"Running\"") != NULL) {
                    ips[0] = '\0';
                    if (tailscale_getips(sd, ips, sizeof(ips)) == 0) {
                        tailnet_connected = 1;
                        reconnect_pending = 0;
                        tsp5_dashboard_set_connected(ips);
                        ps5_log("INFO", "LIFECYCLE_RECONNECTED ips=%s", ips);
                    }
                }
                free(status_json);
            }
        }

        keepmain_set_active(tailnet_connected || reconnect_pending,
                            &keepmain_control_state);

        if (!tailnet_connected) {
            sleep(1);
            elapsed++;
            if ((elapsed % 60) == 0) {
                ps5_log("INFO", "AUTH197_RESIDENT_ALIVE seconds=%d accepted=%d tailnet=disconnected",
                        elapsed, accepted_connections);
            }
            continue;
        }

        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = listener;
        pfd.events = POLLIN;
        errno = 0;
        rc = poll(&pfd, 1, 1000);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            ps5_log("ERROR", "AUTH197_LISTENER_POLL failed errno=%d %s",
                    errno, strerror(errno));
            result = 1;
            goto close_server;
        }
        if (rc == 0) {
            elapsed++;
            if ((elapsed % 60) == 0) {
                ps5_log("INFO", "AUTH197_RESIDENT_ALIVE seconds=%d accepted=%d",
                        elapsed, accepted_connections);
            }
            continue;
        }
        if ((pfd.revents & POLLIN) == 0) {
            ps5_log("ERROR", "AUTH197_LISTENER_POLL unexpected revents=0x%x",
                    (unsigned)pfd.revents);
            result = 1;
            goto close_server;
        }

        ps5_log("INFO", "AUTH197_LISTENER_READABLE entering nonblocking-ready accept");
        rc = tailscale_accept(listener, &inbound_conn);
        if (rc != 0) {
            log_ts_error(sd, "AUTH197 tailscale_accept");
            result = 1;
            goto close_server;
        }
        {
            char remote[256] = "unknown";
            char request[1024];
            const char response[] =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 20\r\n"
                "Connection: close\r\n"
                "\r\n"
                "TSP5 PS5 tailnet OK\n";
            struct timeval tv;
            ssize_t received;
            ssize_t sent;

            if (tailscale_getremoteaddr(listener, inbound_conn, remote,
                                        sizeof(remote)) != 0) {
                (void)snprintf(remote, sizeof(remote), "unavailable");
            }
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            (void)setsockopt(inbound_conn, SOL_SOCKET, SO_RCVTIMEO,
                             &tv, sizeof(tv));
            (void)setsockopt(inbound_conn, SOL_SOCKET, SO_SNDTIMEO,
                             &tv, sizeof(tv));
            errno = 0;
            received = recv(inbound_conn, request, sizeof(request), 0);
            ps5_log("INFO", "AUTH197_INBOUND_ACCEPT remote=%s fd=%d recv=%lld errno=%d",
                    remote, inbound_conn, (long long)received, errno);
            errno = 0;
            sent = send(inbound_conn, response, sizeof(response) - 1, 0);
            ps5_log("INFO", "AUTH197_INBOUND_RESPONSE sent=%lld expected=%llu errno=%d",
                    (long long)sent,
                    (unsigned long long)(sizeof(response) - 1), errno);
            if (sent != (ssize_t)(sizeof(response) - 1)) {
                result = 1;
                (void)close(inbound_conn);
                inbound_conn = -1;
                goto close_server;
            }
        }
        (void)close(inbound_conn);
        inbound_conn = -1;
        accepted_connections++;
        ps5_log("INFO", "AUTH197_INBOUND_SERVICE_OK accepted=%d",
                accepted_connections);
    }

close_server:
    if (inbound_conn >= 0) {
        (void)close(inbound_conn);
        inbound_conn = -1;
    }
    if (listener >= 0) {
        (void)close(listener);
        listener = -1;
    }
    if (sd >= 0) {
        atomic_store(&close_watchdog_armed, 1);
        watchdog_rc = pthread_create(&close_watchdog_thread, NULL,
                                     close_watchdog_main, NULL);
        if (watchdog_rc == 0) {
            (void)pthread_detach(close_watchdog_thread);
            ps5_log("INFO", "AUTH_CLOSE_WATCHDOG armed timeout=20s");
        } else {
            atomic_store(&close_watchdog_armed, 0);
            ps5_log("ERROR", "AUTH_CLOSE_WATCHDOG pthread_create failed rc=%d", watchdog_rc);
        }
        rc = tailscale_close(sd);
        atomic_store(&close_watchdog_armed, 0);
        ps5_log("INFO", "AUTH_CLOSE_WATCHDOG disarmed after tailscale_close return");
        if (rc != 0) {
            ps5_log("ERROR", "tailscale_close failed rc=%d", rc);
            result = 1;
        } else {
            ps5_log("INFO", "tailscale_close passed");
        }
    }

done:
    keepmain_set_active(0, &keepmain_control_state);
    if (poll_trace_fd >= 0) {
        (void)TsnetPS5SetPollTraceFD(-1);
        (void)close(poll_trace_fd);
    }
    if (tslog_fd >= 0) {
        (void)close(tslog_fd);
    }
    ps5_log(result == 0 ? "INFO" : "ERROR",
            "libtailscale auth/IP probe finished result=%d", result);
    ps5_log_close();
    return result;
}
