#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <ps5/kernel.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct notify_request {
    char reserved[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
int sceNetInit(void);
int sceNetCtlInit(void);
int sceUserServiceInitialize(int *);
int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallAll(void *);

#define TITLE_ID "TSCL00001"
#define ROOT "/user/app/" TITLE_ID
#define SCE_SYS ROOT "/sce_sys"
#define PARAM SCE_SYS "/param.json"
#define ICON SCE_SYS "/icon0.png"
#define LOG_PATH "/data/tailscale/bootstrap.log"
#define STATE_DIR "/data/tailscale/libtailscale-auth133-fresh"
#define STATE_FILE STATE_DIR "/tailscaled.state"
#define CURRENT_PROFILE STATE_DIR "/_current-profile"
#define REGISTERED_MARKER STATE_DIR "/.tsp5-registered"
#define RESET_REQUEST "/data/tailscale/reset-registration.request"
#define AUTHKEY_PATH "/data/tailscale/ps5-auth.key"
#define CA_BUNDLE_PATH "/data/tailscale/ca-certificates.crt"
#define ELFLDR_PORT 9021
#define KEEPMAIN_STATUS_PATH "/data/tailscale/keepmain-injector.status"
#define KEEPMAIN_INJECT_ATTEMPTS 5U
#define KEEPMAIN_INJECT_RETRY_US 1000000U
#ifdef TSP5_SHELLCORE_COMPANION
#define SHELLCORE_KEEPMAIN_STATUS_PATH "/data/tailscale/keepmain-shellcore-injector.status"
#endif

#define INCASSET(name, file)                                                   \
    __asm__(".section .rodata\n"                                             \
            ".global " #name "\n"                                            \
            ".global " #name "_end\n"                                        \
            ".global " #name "_size\n"                                       \
            ".align 16\n" #name ":\n"                                       \
            ".incbin \"" file "\"\n" #name "_end:\n" #name "_size:\n"     \
            ".quad " #name "_end - " #name "\n"                              \
            ".previous\n");                                                   \
    extern const uint8_t name[];                                               \
    extern const size_t name##_size

INCASSET(param_json, "assets/tailscale-media/param.json");
INCASSET(icon_png, "assets/tailscale-media/icon0.png");
INCASSET(ca_bundle, "assets/tailscale-ca/ca-certificates.crt");
#ifdef TSP5_V103_RELEASE_CORE_ASSET
INCASSET(tailscale_core_elf, "build/ps5-libtailscale-auth197-core-v1.0.3.elf");
#elif defined(TSP5_FIELD_DIAGNOSTIC_CORE_ASSET)
INCASSET(tailscale_core_elf, "build/ps5-libtailscale-auth197-core-field-diagnostic.elf");
#elif defined(TSP5_SCEJSCD_TAILSCALE_TEST_CORE_ASSET)
INCASSET(tailscale_core_elf, "build/ps5-libtailscale-auth197-core-scejscd-tailscale-test.elf");
#elif defined(TSP5_CANDIDATE_CORE_ASSET)
INCASSET(tailscale_core_elf, "build/ps5-libtailscale-auth197-core-candidate.elf");
#else
INCASSET(tailscale_core_elf, "build/ps5-libtailscale-auth197-core.elf");
#endif
INCASSET(keepmain_injector_elf, "build/keepmain/ps5-keepmain-resident-injector.elf");
#ifdef TSP5_SHELLCORE_COMPANION
INCASSET(shellcore_keepmain_injector_elf, "build/keepmain-shellcore/ps5-keepmain-shellcore-injector.elf");
#endif

static void phase(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int fd;
    int n;

    (void)mkdir("/data/tailscale", 0777);
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;

    fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        (void)write(fd, buf, (size_t)n);
        (void)write(fd, "\n", 1);
        (void)fsync(fd);
        (void)close(fd);
    }
    (void)write(STDOUT_FILENO, buf, (size_t)n);
    (void)write(STDOUT_FILENO, "\n", 1);
}

static void notify(const char *msg)
{
    notify_request_t r;
    memset(&r, 0, sizeof(r));
    (void)snprintf(r.message, sizeof(r.message), "%s", msg);
    (void)sceKernelSendNotificationRequest(0, &r, sizeof(r), 0);
}

static int write_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(data, 1, size, f) != size) {
        int e = errno;
        (void)fclose(f);
        errno = e ? e : EIO;
        return -1;
    }
    return fclose(f);
}

typedef int (*install_title_dir_fn)(const char *, const char *, void *);

static int path_exists(const char *path);

static int ensure_ca_bundle(void)
{
    if (path_exists(CA_BUNDLE_PATH)) {
        phase("BOOTSTRAP CA bundle present path=%s action=keep", CA_BUNDLE_PATH);
        return 0;
    }
    if (write_file(CA_BUNDLE_PATH, ca_bundle, ca_bundle_size) != 0) {
        phase("BOOTSTRAP CA bundle create failed path=%s errno=%d",
              CA_BUNDLE_PATH, errno);
        return -1;
    }
    phase("BOOTSTRAP CA bundle created path=%s bytes=%zu",
          CA_BUNDLE_PATH, ca_bundle_size);
    return 0;
}

static int path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void apply_registration_reset_if_requested(void)
{
    if (!path_exists(RESET_REQUEST)) {
        return;
    }

    phase("BOOTSTRAP registration reset requested");
    if (unlink(STATE_FILE) != 0 && errno != ENOENT) {
        phase("BOOTSTRAP reset state unlink failed path=%s errno=%d", STATE_FILE, errno);
    }
    if (unlink(CURRENT_PROFILE) != 0 && errno != ENOENT) {
        phase("BOOTSTRAP reset current-profile unlink failed errno=%d", errno);
    }
    if (unlink(REGISTERED_MARKER) != 0 && errno != ENOENT) {
        phase("BOOTSTRAP reset marker unlink failed errno=%d", errno);
    }
    if (unlink(AUTHKEY_PATH) != 0 && errno != ENOENT) {
        phase("BOOTSTRAP reset auth-key unlink failed errno=%d", errno);
    }
    if (unlink(RESET_REQUEST) != 0 && errno != ENOENT) {
        phase("BOOTSTRAP reset request unlink failed errno=%d", errno);
    }
    phase("BOOTSTRAP registration reset applied");
}

static int install_media_launcher(void)
{
    int rc;
    int user_prio = 256;
    uint32_t handle = 0;
    install_title_dir_fn title_dir = NULL;

    errno = 0;
    rc = sceNetCtlInit();
    phase("BOOTSTRAP sceNetCtlInit rc=0x%08x errno=%d", (unsigned)rc, errno);

    errno = 0;
    rc = sceUserServiceInitialize(&user_prio);
    phase("BOOTSTRAP sceUserServiceInitialize rc=0x%08x errno=%d", (unsigned)rc, errno);

    errno = 0;
    rc = sceNetInit();
    phase("BOOTSTRAP sceNetInit rc=0x%08x errno=%d", (unsigned)rc, errno);

    if (mkdir(ROOT, 0755) != 0 && errno != EEXIST) {
        phase("BOOTSTRAP mkdir root failed errno=%d", errno);
        return -1;
    }
    if (mkdir(SCE_SYS, 0755) != 0 && errno != EEXIST) {
        phase("BOOTSTRAP mkdir sce_sys failed errno=%d", errno);
        return -1;
    }
    if (write_file(PARAM, param_json, param_json_size) != 0) {
        phase("BOOTSTRAP param write failed errno=%d", errno);
        return -1;
    }
    if (write_file(ICON, icon_png, icon_png_size) != 0) {
        phase("BOOTSTRAP icon write failed errno=%d", errno);
        return -1;
    }
    phase("BOOTSTRAP icon staged bytes=%zu", icon_png_size);

    errno = 0;
    rc = sceAppInstUtilInitialize();
    phase("BOOTSTRAP AppInst init rc=0x%08x errno=%d", (unsigned)rc, errno);
    if (rc != 0) return -1;

    errno = 0;
    rc = kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle);
    phase("BOOTSTRAP dynlib handle rc=%d handle=0x%x errno=%d", rc, handle, errno);
    if (rc == 0) {
        title_dir = (install_title_dir_fn)
            kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE");
    }
    phase("BOOTSTRAP title_dir=%p errno=%d", (void *)title_dir, errno);

    if (title_dir != NULL) {
        errno = 0;
        rc = title_dir(TITLE_ID, "/user/app/", NULL);
        phase("BOOTSTRAP TitleDir rc=0x%08x errno=%d", (unsigned)rc, errno);
    } else {
        rc = -1;
    }

    if (rc != 0) {
        int all_rc;
        errno = 0;
        all_rc = sceAppInstUtilAppInstallAll(NULL);
        phase("BOOTSTRAP AppInstallAll rc=0x%08x errno=%d", (unsigned)all_rc, errno);
        if (all_rc == 0) rc = 0;
    }

    errno = 0;
    {
        int trc = sceAppInstUtilTerminate();
        phase("BOOTSTRAP AppInst term rc=0x%08x errno=%d", (unsigned)trc, errno);
    }

    return rc == 0 ? 0 : -1;
}

static int launch_elf(const char *label, const uint8_t *data, size_t size)
{
    int sock;
    int flags;
    struct sockaddr_in addr;
    struct timespec deadline;
    size_t sent_total = 0;

    phase("BOOTSTRAP launching %s bytes=%zu", label, size);
    usleep(500000);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        phase("BOOTSTRAP core socket failed errno=%d", errno);
        return -1;
    }

    flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) != 0) {
        phase("BOOTSTRAP %s nonblocking setup failed errno=%d", label, errno);
        (void)close(sock);
        return -1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
        phase("BOOTSTRAP %s clock_gettime failed errno=%d", label, errno);
        (void)close(sock);
        return -1;
    }
    deadline.tv_sec += 30;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ELFLDR_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
            phase("BOOTSTRAP %s connect 127.0.0.1:%d failed errno=%d",
                  label, ELFLDR_PORT, errno);
            (void)close(sock);
            return -1;
        }
        {
            struct pollfd pfd;
            int poll_rc;
            int socket_error = 0;
            socklen_t socket_error_len = sizeof(socket_error);
            memset(&pfd, 0, sizeof(pfd));
            pfd.fd = sock;
            pfd.events = POLLOUT;
            poll_rc = poll(&pfd, 1, 3000);
            if (poll_rc <= 0 ||
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &socket_error,
                           &socket_error_len) != 0 || socket_error != 0) {
                phase("BOOTSTRAP %s connect timeout/error poll=%d socket_error=%d errno=%d",
                      label, poll_rc, socket_error, errno);
                (void)close(sock);
                return -1;
            }
        }
    }

    while (sent_total < size) {
        struct timespec now;
        size_t remain = size - sent_total;
        size_t chunk = remain > 8192 ? 8192 : remain;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
            now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
            phase("BOOTSTRAP %s send overall timeout sent=%zu", label, sent_total);
            (void)close(sock);
            return -1;
        }
        ssize_t n = send(sock, data + sent_total, chunk, 0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            struct pollfd pfd;
            int poll_rc;
            memset(&pfd, 0, sizeof(pfd));
            pfd.fd = sock;
            pfd.events = POLLOUT;
            poll_rc = poll(&pfd, 1, 3000);
            if (poll_rc > 0) continue;
            phase("BOOTSTRAP %s send timeout sent=%zu errno=%d",
                  label, sent_total, errno);
            (void)close(sock);
            return -1;
        }
        if (n <= 0) {
            phase("BOOTSTRAP %s send failed sent=%zu errno=%d",
                  label, sent_total, errno);
            (void)close(sock);
            return -1;
        }
        sent_total += (size_t)n;
    }

    (void)close(sock);
    phase("BOOTSTRAP %s sent bytes=%zu", label, sent_total);
    return 0;
}

static int wait_keepmain_status(const char *label, const char *status_path)
{
    int attempts;
    for (attempts = 0; attempts < 50; attempts++) {
        FILE *f = fopen(status_path, "rb");
        if (f != NULL) {
            char status[96];
            status[0] = '\0';
            if (fgets(status, sizeof(status), f) != NULL) {
                status[strcspn(status, "\r\n")] = '\0';
            }
            (void)fclose(f);
            phase("BOOTSTRAP %s status=%s", label, status);
            return strncmp(status, "ok:", 3) == 0 ? 0 : -1;
        }
        usleep(100000);
    }
    phase("BOOTSTRAP %s status timeout path=%s", label, status_path);
    return -1;
}

static int launch_keepmain_treatment(void)
{
    unsigned attempt;
    for (attempt = 1U; attempt <= KEEPMAIN_INJECT_ATTEMPTS; attempt++) {
        (void)unlink(KEEPMAIN_STATUS_PATH);
        phase("BOOTSTRAP SceJSCd KeepMain injector attempt=%u/%u",
              attempt, KEEPMAIN_INJECT_ATTEMPTS);
        if (launch_elf("KeepMain injector", keepmain_injector_elf,
                       keepmain_injector_elf_size) == 0 &&
            wait_keepmain_status("SceJSCd KeepMain injector",
                                 KEEPMAIN_STATUS_PATH) == 0) {
            return 0;
        }
        if (attempt < KEEPMAIN_INJECT_ATTEMPTS) {
            phase("BOOTSTRAP SceJSCd KeepMain injector retrying after transient failure delay_ms=%u",
                  KEEPMAIN_INJECT_RETRY_US / 1000U);
            usleep(KEEPMAIN_INJECT_RETRY_US);
        }
    }
    return -1;
}

#ifdef TSP5_SHELLCORE_COMPANION
static int launch_shellcore_keepmain_companion(void)
{
    (void)unlink(SHELLCORE_KEEPMAIN_STATUS_PATH);
    if (launch_elf("SceShellCore KeepMain companion injector",
                   shellcore_keepmain_injector_elf,
                   shellcore_keepmain_injector_elf_size) != 0) {
        return -1;
    }
    return wait_keepmain_status("SceShellCore KeepMain companion injector",
                                SHELLCORE_KEEPMAIN_STATUS_PATH);
}
#endif

static int launch_core(void)
{
    return launch_elf("tailscale core", tailscale_core_elf,
                      tailscale_core_elf_size);
}

int main(void)
{
    (void)unlink(LOG_PATH);
    phase("BOOTSTRAP START pid=%d flow=elf->ca->icon->tailscale", (int)getpid());
    apply_registration_reset_if_requested();
    if (ensure_ca_bundle() != 0) {
        notify("Tailscale: CA bundle setup failed");
        return 15;
    }
    notify("Tailscale: installing launcher");

    if (install_media_launcher() != 0) {
        phase("BOOTSTRAP launcher registration FAILED; tailscale not started");
        notify("Tailscale: launcher install failed");
        return 20;
    }

    phase("BOOTSTRAP launcher registration OK");
    notify("Tailscale launcher ready; starting SceJSCd KeepMain");

    if (launch_keepmain_treatment() != 0) {
        phase("BOOTSTRAP SceJSCd KeepMain resident launch FAILED; tailscale not started");
        notify("Tailscale: SceJSCd KeepMain setup failed");
        return 25;
    }
    phase("BOOTSTRAP SceJSCd KeepMain resident ready lifetime=until-reboot");
#ifdef TSP5_SHELLCORE_COMPANION
    notify("Tailscale SceJSCd KeepMain ready; starting SceShellCore companion");
    if (launch_shellcore_keepmain_companion() != 0) {
        phase("BOOTSTRAP SceShellCore KeepMain companion unavailable; continuing test with required SceJSCd resident");
        notify("Tailscale: SceShellCore companion unavailable; continuing test");
    } else {
        phase("BOOTSTRAP SceShellCore KeepMain companion ready lifecycle=core-controlled");
    }
#endif
    notify("Tailscale SceJSCd KeepMain ready; starting service");

    if (launch_core() != 0) {
        phase("BOOTSTRAP tailscale core launch FAILED");
        notify("Tailscale: core launch failed");
        return 30;
    }

    phase("BOOTSTRAP COMPLETE");
    notify("Tailscale service started");
    return 0;
}
