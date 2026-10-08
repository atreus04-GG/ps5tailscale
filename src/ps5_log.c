#include "ps5_log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef TSP5_REMOTE_LOG_HOST
/* Public builds keep the native network-init path but send optional UDP
   diagnostics only to loopback unless a developer explicitly overrides it. */
#define TSP5_REMOTE_LOG_HOST "127.0.0.1"
#endif
#ifndef TSP5_REMOTE_LOG_PORT
#define TSP5_REMOTE_LOG_PORT 9025
#endif

int sceNetInit(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceNetPoolDestroy(int memid);
int sceNetTerm(void);

static int log_fd = -1;
static int log_init_error;
static int remote_fd = -1;
static int remote_pool = -1;
static int remote_net_started;
static struct sockaddr_in remote_endpoint;

static void
remote_log_init(void)
{
    int flags;
    int rc = sceNetInit();
    if (rc != 0) {
        fprintf(stderr, "[TSP5] remote log sceNetInit failed: %d\n", rc);
        return;
    }
    remote_net_started = 1;
    remote_pool = sceNetPoolCreate("tsp5-remote-log", 64 * 1024, 0);
    if (remote_pool < 0) {
        fprintf(stderr, "[TSP5] remote log pool failed: %d\n", remote_pool);
        return;
    }
    remote_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (remote_fd < 0) {
        fprintf(stderr, "[TSP5] remote log socket failed: %s\n", strerror(errno));
        return;
    }
    memset(&remote_endpoint, 0, sizeof(remote_endpoint));
    remote_endpoint.sin_family = AF_INET;
#ifdef __FreeBSD__
    remote_endpoint.sin_len = sizeof(remote_endpoint);
#endif
    remote_endpoint.sin_port = htons(TSP5_REMOTE_LOG_PORT);
    if (inet_pton(AF_INET, TSP5_REMOTE_LOG_HOST, &remote_endpoint.sin_addr) != 1) {
        fprintf(stderr, "[TSP5] invalid remote log host %s\n", TSP5_REMOTE_LOG_HOST);
        close(remote_fd);
        remote_fd = -1;
        return;
    }
    if (connect(remote_fd, (const struct sockaddr *)&remote_endpoint,
                sizeof(remote_endpoint)) != 0) {
        fprintf(stderr, "[TSP5] remote log connect failed: %s\n", strerror(errno));
        close(remote_fd);
        remote_fd = -1;
        return;
    }
    flags = fcntl(remote_fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(remote_fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static void
remote_log_close(void)
{
    if (remote_fd >= 0) {
        close(remote_fd);
        remote_fd = -1;
    }
    if (remote_pool >= 0) {
        (void)sceNetPoolDestroy(remote_pool);
        remote_pool = -1;
    }
    if (remote_net_started) {
        (void)sceNetTerm();
        remote_net_started = 0;
    }
}

int
ps5_log_init(void)
{
    log_init_error = 0;
    remote_log_init();

    if (mkdir("/data/tailscale", 0777) != 0 && errno != EEXIST) {
        log_init_error = errno;
        fprintf(stderr, "[TSP5] cannot create /data/tailscale: %s\n", strerror(log_init_error));
        return -1;
    }

    log_fd = open("/data/tailscale/tailscale.log",
                  O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (log_fd < 0) {
        log_init_error = errno;
        fprintf(stderr, "[TSP5] cannot open log: %s\n", strerror(log_init_error));
        return -1;
    }
    return 0;
}
int
ps5_log_init_error(void)
{
    return log_init_error;
}

int
ps5_log_remote_dup(void)
{
    int fd;
    int flags;

    if (remote_fd < 0) {
        return -1;
    }

    /* Use a separate socket for Go/tsnet logs. A dup would share the same
       socket state as ps5_log(). Keep this one non-blocking so diagnostic
       logging can never stall tailscale_start if the UDP send queue fills. */
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    if (connect(fd, (const struct sockaddr *)&remote_endpoint,
                sizeof(remote_endpoint)) != 0) {
        close(fd);
        return -1;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    return fd;
}

long long
ps5_log_file_size(void)
{
    struct stat st;
    if (stat("/data/tailscale/tailscale.log", &st) != 0) {
        return -1;
    }
    return (long long)st.st_size;
}

void
ps5_log_close(void)
{
    if (log_fd >= 0) {
        (void)fsync(log_fd);
        close(log_fd);
        log_fd = -1;
    }
    remote_log_close();
}

void
ps5_log(const char *level, const char *format, ...)
{
    char message[1024];
    char timestamp[32] = "time-unavailable";
    char line[1200];
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    va_list args;
    int line_len;

    if (utc != NULL) {
        (void)strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", utc);
    }

    va_start(args, format);
    (void)vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    line_len = snprintf(line, sizeof(line), "%s [%s] TSP5 %s\n",
                        timestamp, level, message);
    if (line_len <= 0) {
        return;
    }
    fputs(line, stderr);
    fflush(stderr);

    if (remote_fd >= 0) {
        (void)send(remote_fd, line, (size_t)line_len, 0);
    }

    if (log_fd >= 0) {
        size_t remaining = (size_t)line_len;
        const char *cursor = line;
        while (remaining > 0) {
            ssize_t written = write(log_fd, cursor, remaining);
            if (written <= 0) {
                break;
            }
            cursor += written;
            remaining -= (size_t)written;
        }
        (void)fsync(log_fd);
    }
}
