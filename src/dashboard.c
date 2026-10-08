#include "dashboard.h"
#include "homebrew_keepalive.h"
#include "ps5_log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define DASHBOARD_PORT 8085
#define STATE_DIR "/data/tailscale/libtailscale-auth133-fresh"
#define CURRENT_PROFILE STATE_DIR "/_current-profile"
#define REGISTERED_MARKER STATE_DIR "/.tsp5-registered"
#define RESET_REQUEST "/data/tailscale/reset-registration.request"
#define AUTHKEY_PATH "/data/tailscale/ps5-auth.key"
#define DASHBOARD_RETRY_SECONDS 2U

static atomic_int dashboard_started;
static atomic_int register_requested;
static atomic_int lifecycle_command;
static pthread_t dashboard_thread;
static pthread_mutex_t dashboard_state_lock = PTHREAD_MUTEX_INITIALIZER;
static char dashboard_state[32] = "Not Registered";
static char dashboard_ips[256] = "";
static char dashboard_error[256] = "";

static void set_state(const char *state, const char *ips, const char *error);

static void render_homebrew_keepalive(char *out, size_t out_size)
{
    tsp5_homebrew_keepalive_status_t status;

    tsp5_homebrew_keepalive_status(&status);
    (void)snprintf(out, out_size,
        "<!-- HOMEBREW_KEEPALIVE_EXPERIMENTAL -->"
        "<div class='keepalive'>"
        "<div><strong>Experimental Homebrew Keepalive</strong> <span class='experimental'>Experimental</span></div>"
        "<p class='muted'>Compatible homebrew that is already running may remain reachable while the PS5 is in Rest Mode.</p>"
        "<div class='row'><div class='muted'>Rest Mode Keepalive</div><div class='value'>%s</div></div>"
        "<div class='row'><div class='muted'>Rest Mode Companion</div><div class='value'>%s / %s</div></div>"
        "<div class='muted'>Verified compatible in hardware testing</div>"
        "<div class='service-row'><span>ELF Loader <code>9021</code></span><strong>Verified</strong></div>"
        "<div class='service-row'><span>FTP <code>2121</code></span><strong>Verified</strong></div>"
        "<div class='service-row'><span>PKG Manager <code>8844</code></span><strong>Verified</strong></div>"
        "<div class='service-row'><span>Remote Play <code>9295</code></span><strong>Verified</strong></div>"
        "<p class='muted'>These labels report known compatibility, not a live connection test. The dashboard does not connect to or control these services.</p>"
        "</div>",
        status.keepmain_resident_initialized ? "Initialized" : "Not initialized",
        status.shellcore_companion_initialized ? "Initialized" : "Not initialized",
        status.shellcore_requested_active ? "Active" : "Inactive");
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int tsp5_dashboard_identity_present(void)
{
    return file_exists(REGISTERED_MARKER) || file_exists(CURRENT_PROFILE);
}

int tsp5_dashboard_mark_registered(void)
{
    FILE *f = fopen(REGISTERED_MARKER, "wb");
    if (f == NULL) {
        ps5_log("ERROR", "ENROLLMENT marker create failed path=%s errno=%d",
                REGISTERED_MARKER, errno);
        return -1;
    }
    if (fwrite("registered\n", 1, 11, f) != 11) {
        int saved_errno = errno;
        (void)fclose(f);
        errno = saved_errno;
        ps5_log("ERROR", "ENROLLMENT marker write failed path=%s errno=%d",
                REGISTERED_MARKER, errno);
        return -1;
    }
    if (fclose(f) != 0) {
        ps5_log("ERROR", "ENROLLMENT marker close failed path=%s errno=%d",
                REGISTERED_MARKER, errno);
        return -1;
    }
    (void)unlink(RESET_REQUEST);
    ps5_log("INFO", "ENROLLMENT marker created path=%s", REGISTERED_MARKER);
    return 0;
}

int tsp5_dashboard_request_registration_reset(void)
{
    FILE *f = fopen(RESET_REQUEST, "wb");
    if (f == NULL) {
        ps5_log("ERROR", "ENROLLMENT reset request create failed path=%s errno=%d",
                RESET_REQUEST, errno);
        return -1;
    }
    if (fwrite("reset\n", 1, 6, f) != 6 || fclose(f) != 0) {
        ps5_log("ERROR", "ENROLLMENT reset request write failed path=%s errno=%d",
                RESET_REQUEST, errno);
        return -1;
    }

    (void)unlink(REGISTERED_MARKER);
    (void)unlink(AUTHKEY_PATH);
    set_state("Reset Requested", "", "Restart the PS5 before registering again.");
    ps5_log("INFO", "ENROLLMENT reset requested path=%s", RESET_REQUEST);
    return 0;
}

static int reset_pending(void)
{
    return file_exists(RESET_REQUEST);
}

int tsp5_dashboard_authkey_present(void)
{
    return file_exists(AUTHKEY_PATH);
}

static void set_state(const char *state, const char *ips, const char *error)
{
    (void)pthread_mutex_lock(&dashboard_state_lock);
    (void)snprintf(dashboard_state, sizeof(dashboard_state), "%s", state ? state : "");
    (void)snprintf(dashboard_ips, sizeof(dashboard_ips), "%s", ips ? ips : "");
    (void)snprintf(dashboard_error, sizeof(dashboard_error), "%s", error ? error : "");
    (void)pthread_mutex_unlock(&dashboard_state_lock);
}

int tsp5_dashboard_get_lan_ipv4(char *out, size_t out_size)
{
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa;

    if (out == NULL || out_size == 0) {
        return -1;
    }
    out[0] = '\0';

    if (getifaddrs(&ifaddr) != 0) {
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        struct sockaddr_in *sin;
        const char *text;

        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        sin = (struct sockaddr_in *)ifa->ifa_addr;
        if (ntohl(sin->sin_addr.s_addr) == INADDR_LOOPBACK ||
            sin->sin_addr.s_addr == htonl(INADDR_ANY)) {
            continue;
        }

        text = inet_ntop(AF_INET, &sin->sin_addr, out, (socklen_t)out_size);
        if (text != NULL && out[0] != '\0') {
            freeifaddrs(ifaddr);
            return 0;
        }
    }

    freeifaddrs(ifaddr);
    out[0] = '\0';
    return -1;
}

void tsp5_dashboard_set_connecting(void)
{
    set_state("Connecting", "", "");
}

void tsp5_dashboard_set_connected(const char *ips)
{
    set_state("Connected", ips, "");
}

void tsp5_dashboard_set_disconnected(void)
{
    set_state("Disconnected", "", "");
}

int tsp5_dashboard_take_lifecycle_command(void)
{
    return atomic_exchange(&lifecycle_command, 0);
}

void tsp5_dashboard_set_error(const char *message)
{
    set_state("Error", "", message ? message : "Unknown error");
}

static int write_authkey(const char *value)
{
    char key[512];
    size_t n;
    FILE *f;

    if (value == NULL) {
        return -1;
    }
    n = strcspn(value, "&\r\n");
    if (n == 0 || n >= sizeof(key)) {
        return -1;
    }
    memcpy(key, value, n);
    key[n] = '\0';

    if (strncmp(key, "tskey-", 6) != 0) {
        memset(key, 0, sizeof(key));
        return -1;
    }

    f = fopen(AUTHKEY_PATH, "wb");
    if (f == NULL) {
        memset(key, 0, sizeof(key));
        return -1;
    }
    if (fwrite(key, 1, n, f) != n || fwrite("\n", 1, 1, f) != 1) {
        (void)fclose(f);
        memset(key, 0, sizeof(key));
        return -1;
    }
    if (fclose(f) != 0) {
        memset(key, 0, sizeof(key));
        return -1;
    }

    memset(key, 0, sizeof(key));
    return 0;
}

static void send_all(int fd, const char *data, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, data + off, len - off, 0);
        if (n <= 0) {
            return;
        }
        off += (size_t)n;
    }
}

static void http_response(int fd, const char *status, const char *content_type,
                          const char *body)
{
    char head[512];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "Cache-Control: no-store\r\n"
                     "\r\n",
                     status, content_type, strlen(body));
    if (n > 0) {
        send_all(fd, head, (size_t)n);
        send_all(fd, body, strlen(body));
    }
}

static void render_dashboard(char *out, size_t out_size)
{
    int identity = tsp5_dashboard_identity_present();
    int key_present = tsp5_dashboard_authkey_present();
    char state[32];
    char ips[256];
    char error[256];
    char lifecycle_html[512] = "";
    char keepalive_html[2048] = "";
    char lan_ip[64] = "";
    char lan_url[128] = "Unavailable until the PS5 has a LAN IP";

    (void)tsp5_dashboard_get_lan_ipv4(lan_ip, sizeof(lan_ip));
    if (lan_ip[0] != '\0') {
        (void)snprintf(lan_url, sizeof(lan_url), "http://%s:%d/", lan_ip, DASHBOARD_PORT);
    }
    render_homebrew_keepalive(keepalive_html, sizeof(keepalive_html));

    if (reset_pending()) {
        (void)snprintf(out, out_size,
            "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>PS5 Tailscale</title><style>"
            "body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:24px}"
            ".card{max-width:700px;margin:auto;background:#1b1b1b;padding:28px;border-radius:18px}"
            ".warn{color:#f3c969}.muted{color:#aaa}.row{margin:18px 0}.value{font-size:1.05rem;word-break:break-all}"
            "</style></head><body><div class='card'>"
            "<h1>PS5 Tailscale</h1>"
            "<h2 class='warn'>Restart PS5 required</h2>"
            "<p>The previous Tailscale registration has been marked for reset.</p>"
            "<p><strong>Do not enter or upload a new auth key yet.</strong></p>"
            "<p>Reboot the PS5, jailbreak/start the ELF loader, then launch Tailscale again. The bootstrap will clear the old Tailscale state before the new core starts.</p>"
            "<div class='row'><div class='muted'>Dashboard</div><div class='value'>%s</div></div>"
            "<p class='muted'>After the reboot and relaunch, this page will return to the registration screen.</p>"
            "<p class='muted' style='margin-top:28px'>Created by atreus04-GG</p>"
            "</div></body></html>",
            lan_url);
        return;
    }

    (void)pthread_mutex_lock(&dashboard_state_lock);
    (void)snprintf(state, sizeof(state), "%s", dashboard_state);
    (void)snprintf(ips, sizeof(ips), "%s", dashboard_ips);
    (void)snprintf(error, sizeof(error), "%s", dashboard_error);
    (void)pthread_mutex_unlock(&dashboard_state_lock);

    if (strcmp(state, "Disconnected") == 0) {
        (void)snprintf(lifecycle_html, sizeof(lifecycle_html),
                       "<form method='post' action='/api/reconnect'><button type='submit'>Reconnect</button></form>");
    } else if (strcmp(state, "Connected") == 0) {
        (void)snprintf(lifecycle_html, sizeof(lifecycle_html),
                       "<form method='post' action='/api/disconnect'><button type='submit'>Disconnect</button></form>");
    }

    if (identity || strcmp(state, "Connected") == 0 || strcmp(state, "Connecting") == 0 ||
        strcmp(state, "Restoring Identity") == 0) {
        (void)snprintf(out, out_size,
            "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>PS5 Tailscale</title><style>"
            "body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:24px}"
            ".card{max-width:700px;margin:auto;background:#1b1b1b;padding:28px;border-radius:18px}"
            ".ok{color:#65d68a}.experimental{color:#f3c969}.muted{color:#aaa}.row{margin:18px 0}.value{font-size:1.1rem;word-break:break-all}"
            ".keepalive{margin:24px 0;padding:18px;border:1px solid #444;border-radius:12px}.service-row{display:flex;justify-content:space-between;gap:18px;margin:10px 0}"
            "code{background:#292929;padding:3px 6px;border-radius:5px}"
            "button{font-size:1rem;padding:12px 18px;border-radius:10px;border:0;margin-top:10px}"
            ".danger{background:#b33;color:#fff}</style></head><body><div class='card'>"
            "<h1>PS5 Tailscale</h1><div class='row'><span class='ok'>&#9679;</span> %s</div>"
            "<div class='row'><div class='muted'>Tailscale IP</div><div class='value'>%s</div></div>"
            "<div class='row'><div class='muted'>Open this dashboard from a PC on the same network</div><div class='value'>%s</div></div>"
            "%s"
            "%s"
            "%s"
            "<form method='post' action='/api/reset' onsubmit=\"return confirm('Reset Tailscale registration on this PS5? You will need to restart the app and register again.');\">"
            "<button class='danger' type='submit'>Reset registration</button></form>"
            "<p class='muted'>Reset removes the saved enrollment marker and auth key now, then clears the Tailscale state before the next launch.</p>"
            "<p class='muted' style='margin-top:28px'>Created by atreus04-GG</p>"
            "</div></body></html>",
            state,
            ips[0] ? ips : "Waiting for address...",
            lan_url,
            keepalive_html,
            error[0] ? error : "",
            lifecycle_html);
        return;
    }

    (void)snprintf(out, out_size,
        "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>PS5 Tailscale</title><style>"
        "body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:24px}"
        ".card{max-width:700px;margin:auto;background:#1b1b1b;padding:28px;border-radius:18px}"
        ".muted{color:#aaa}.row{margin:18px 0}.value{font-size:1.05rem;word-break:break-all}"
        "input{width:100%%;box-sizing:border-box;font-size:1rem;padding:12px;border-radius:10px;border:1px solid #555;background:#111;color:#eee}"
        "button,.buttonlink{font-size:1rem;padding:12px 20px;border-radius:10px;border:0;margin-top:12px;display:inline-block;text-decoration:none}"
        ".buttonlink{background:#eee;color:#111}.danger{background:#b33;color:#fff}"
        "code{background:#292929;padding:3px 6px;border-radius:5px}</style></head><body><div class='card'>"
        "<h1>PS5 Tailscale</h1><p>&#9675; Not Registered</p>"
        "<div class='row'><div class='muted'>Open this registration page from a PC or phone on the same network</div><div class='value'>%s</div></div>"
        "<div class='row'><strong>How to get a Tailscale auth key</strong>"
        "<ol><li>Sign in to the Tailscale Admin Console.</li>"
        "<li>Open <strong>Settings &rarr; Keys</strong>.</li>"
        "<li>Under <strong>Auth keys</strong>, choose <strong>Generate auth key</strong>.</li>"
        "<li>Copy the generated key, paste it below, then choose <strong>Save &amp; Register</strong>.</li></ol>"
        "<p><a class='buttonlink' href='https://login.tailscale.com/admin/settings/keys' target='_blank' rel='noopener noreferrer'>Open Tailscale Keys Page</a></p>"
        "<p class='muted'>Keep your auth key private. A reusable key can be convenient while testing.</p></div>"
        "<div class='row'><strong>%s</strong></div>"
        "%s"
        "<div class='row'><strong>Manual FTP option</strong>"
        "<p>You can also create the auth-key file from your PC using FTP.</p>"
        "<p>Upload a text file containing only your Tailscale auth key to:<br><code>/data/tailscale/ps5-auth.key</code></p>"
        "<p class='muted'>After uploading it, refresh this page and choose Register PS5.</p></div>"
        "<p class='muted' style='margin-top:28px'>Created by atreus04-GG</p>"
        "</div></body></html>",
        lan_url,
        key_present ? "Authentication key found" : "Enter a Tailscale Auth Key",
        key_present
            ? "<p class='muted'>/data/tailscale/ps5-auth.key</p><p>This PS5 is ready to register.</p>"
              "<form method='post' action='/api/register'><button type='submit'>Register PS5</button></form>"
              "<form method='post' action='/api/remove-auth-key' onsubmit=\"return confirm('Remove the saved Tailscale auth key from this PS5?');\">"
              "<button class='danger' type='submit'>Remove Saved Auth Key</button></form>"
              "<p class='muted'>This removes only the saved auth-key file. It does not reset the Tailscale identity or remove the PS5 from your tailnet.</p>"
            : "<form method='post' action='/api/register'><input type='password' name='auth_key' placeholder='tskey-auth-...' autocomplete='off' required><button type='submit'>Save &amp; Register</button></form><p class='muted'>The key will be saved to /data/tailscale/ps5-auth.key</p>");
}

static ssize_t recv_http_request(int fd, char *req, size_t req_size)
{
    size_t used = 0;
    size_t expected = 0;

    memset(req, 0, req_size);
    while (used + 1 < req_size) {
        ssize_t n = recv(fd, req + used, req_size - used - 1, 0);
        char *header_end;
        if (n <= 0) {
            return used > 0 ? (ssize_t)used : n;
        }
        used += (size_t)n;
        req[used] = '\0';

        header_end = strstr(req, "\r\n\r\n");
        if (header_end != NULL) {
            char *cl = strstr(req, "Content-Length:");
            size_t header_bytes = (size_t)(header_end + 4 - req);
            if (cl != NULL && cl < header_end) {
                unsigned long body_bytes = strtoul(cl + strlen("Content-Length:"), NULL, 10);
                expected = header_bytes + (size_t)body_bytes;
            } else {
                expected = header_bytes;
            }
            if (used >= expected) {
                return (ssize_t)used;
            }
        }
    }
    return (ssize_t)used;
}

static void handle_client(int fd)
{
    char req[4096];
    ssize_t n;
    char body[8192];

    n = recv_http_request(fd, req, sizeof(req));
    if (n <= 0) {
        return;
    }

    if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /index.html ", 16) == 0) {
        render_dashboard(body, sizeof(body));
        http_response(fd, "200 OK", "text/html; charset=utf-8", body);
        return;
    }

    if (strncmp(req, "GET /api/status ", 16) == 0) {
        char state[32];
        char ips[256];
        tsp5_homebrew_keepalive_status_t keepalive;
        (void)pthread_mutex_lock(&dashboard_state_lock);
        (void)snprintf(state, sizeof(state), "%s", dashboard_state);
        (void)snprintf(ips, sizeof(ips), "%s", dashboard_ips);
        (void)pthread_mutex_unlock(&dashboard_state_lock);
        tsp5_homebrew_keepalive_status(&keepalive);
        (void)snprintf(body, sizeof(body),
                       "{\"state\":\"%s\",\"registered\":%s,\"authKeyPresent\":%s,\"ips\":\"%s\","
                       "\"homebrewKeepalive\":{\"experimental\":true,"
                       "\"keepMainResidentInitialized\":%s,\"shellCoreCompanionInitialized\":%s,"
                       "\"shellCoreRequestedActive\":%s,\"liveServiceProbing\":false,"
                       "\"verifiedCompatibility\":[\"elfLoader9021\",\"ftp2121\",\"pkgManager8844\",\"remotePlay9295\"]}}",
                       state,
                       tsp5_dashboard_identity_present() ? "true" : "false",
                       tsp5_dashboard_authkey_present() ? "true" : "false",
                       ips,
                       keepalive.keepmain_resident_initialized ? "true" : "false",
                       keepalive.shellcore_companion_initialized ? "true" : "false",
                       keepalive.shellcore_requested_active ? "true" : "false");
        http_response(fd, "200 OK", "application/json", body);
        return;
    }

    if (strncmp(req, "POST /api/remove-auth-key ", 26) == 0) {
        int rc = unlink(AUTHKEY_PATH);
        if (rc != 0 && errno != ENOENT) {
            ps5_log("ERROR", "DASHBOARD_AUTHKEY_REMOVE_FAILED path=%s errno=%d", AUTHKEY_PATH, errno);
            http_response(fd, "500 Internal Server Error", "text/html; charset=utf-8",
                          "<!doctype html><meta charset='utf-8'><title>PS5 Tailscale</title>"
                          "<body style='font-family:sans-serif;background:#111;color:#eee;padding:32px'>"
                          "<h1>PS5 Tailscale</h1><p>Could not remove the saved auth key.</p>"
                          "<p><a href='/' style='display:inline-block;padding:12px 18px;border-radius:10px;background:#eee;color:#111;text-decoration:none'>Back to Main Page</a></p></body>");
            return;
        }
        ps5_log("INFO", "DASHBOARD_AUTHKEY_REMOVED path=%s existed=%s", AUTHKEY_PATH, rc == 0 ? "true" : "false");
        http_response(fd, "200 OK", "text/html; charset=utf-8",
                      "<!doctype html><meta charset='utf-8'><title>PS5 Tailscale</title>"
                      "<body style='font-family:sans-serif;background:#111;color:#eee;padding:32px'>"
                      "<h1>PS5 Tailscale</h1>"
                      "<p>The saved Tailscale auth key has been removed from this PS5.</p>"
                      "<p>This did not reset the Tailscale identity or remove the PS5 from your tailnet.</p>"
                      "<p><a href='/' style='display:inline-block;padding:12px 18px;border-radius:10px;background:#eee;color:#111;text-decoration:none'>Back to Main Page</a></p></body>");
        return;
    }

    if (strncmp(req, "POST /api/register ", 19) == 0) {
        char *payload = strstr(req, "\r\n\r\n");
        if (reset_pending()) {
            http_response(fd, "409 Conflict", "text/plain",
                          "Registration reset is pending. Reboot the PS5 before adding a new auth key.\n");
            ps5_log("INFO", "ENROLLMENT register rejected reset_pending=true");
            return;
        }
        if (tsp5_dashboard_identity_present()) {
            http_response(fd, "409 Conflict", "text/plain", "PS5 is already registered.\n");
            return;
        }
        if (payload != NULL) {
            payload += 4;
        }

        if (!tsp5_dashboard_authkey_present()) {
            char *key = payload ? strstr(payload, "auth_key=") : NULL;
            if (key == NULL) {
                http_response(fd, "400 Bad Request", "text/plain", "Auth key required.\n");
                return;
            }
            key += strlen("auth_key=");
            if (write_authkey(key) != 0) {
                http_response(fd, "400 Bad Request", "text/plain", "Invalid auth key. Expected tskey-...\n");
                return;
            }
            ps5_log("INFO", "DASHBOARD_AUTHKEY_SAVED path=%s", AUTHKEY_PATH);
        }

        atomic_store(&register_requested, 1);
        set_state("Registration Requested", "", "");
        http_response(fd, "200 OK", "text/html; charset=utf-8",
                      "<!doctype html><meta charset='utf-8'><title>PS5 Tailscale</title>"
                      "<body style='font-family:sans-serif;background:#111;color:#eee;padding:32px'>"
                      "<h1>PS5 Tailscale</h1><p>Registration requested.</p>"
                      "<p>Return to the main page to see connection status.</p>"
                      "<p><a href='/' style='display:inline-block;padding:12px 18px;border-radius:10px;background:#eee;color:#111;text-decoration:none'>Back to Main Page</a></p></body>");
        return;
    }

    if (strncmp(req, "POST /api/disconnect ", 21) == 0) {
        atomic_store(&lifecycle_command, 1);
        set_state("Disconnecting", "", "");
        http_response(fd, "200 OK", "text/html; charset=utf-8",
                      "<!doctype html><meta charset='utf-8'><title>PS5 Tailscale</title>"
                      "<body style='font-family:sans-serif;background:#111;color:#eee;padding:32px'>"
                      "<h1>PS5 Tailscale</h1><p>Disconnect requested.</p>"
                      "<p>Return to the main page in a moment to check the updated status.</p>"
                      "<p><a href='/' style='display:inline-block;padding:12px 18px;border-radius:10px;background:#eee;color:#111;text-decoration:none'>Back to Main Page</a></p></body>");
        return;
    }

    if (strncmp(req, "POST /api/reconnect ", 20) == 0) {
        atomic_store(&lifecycle_command, 2);
        set_state("Connecting", "", "");
        http_response(fd, "200 OK", "text/html; charset=utf-8",
                      "<!doctype html><meta charset='utf-8'><title>PS5 Tailscale</title>"
                      "<body style='font-family:sans-serif;background:#111;color:#eee;padding:32px'>"
                      "<h1>PS5 Tailscale</h1><p>Reconnect requested.</p>"
                      "<p>Return to the main page in a moment to check the updated status.</p>"
                      "<p><a href='/' style='display:inline-block;padding:12px 18px;border-radius:10px;background:#eee;color:#111;text-decoration:none'>Back to Main Page</a></p></body>");
        return;
    }

    if (strncmp(req, "POST /api/reset ", 16) == 0) {
        if (tsp5_dashboard_request_registration_reset() != 0) {
            http_response(fd, "500 Internal Server Error", "text/plain",
                          "Could not request registration reset.\n");
            return;
        }
        http_response(fd, "200 OK", "text/html; charset=utf-8",
                      "<!doctype html><meta charset='utf-8'><title>PS5 Tailscale</title>"
                      "<body style='font-family:sans-serif;background:#111;color:#eee;padding:32px'>"
                      "<h1>PS5 Tailscale</h1><p>Registration reset requested.</p>"
                      "<p><strong>Reboot the PS5 before adding a new auth key.</strong></p>"
                      "<p>After reboot, jailbreak/start the ELF loader and launch Tailscale again. The bootstrap will clear the old Tailscale state before the new core starts.</p>"
                      "<p><a href='/' style='display:inline-block;padding:12px 18px;border-radius:10px;background:#eee;color:#111;text-decoration:none'>Back to Main Page</a></p>"
                      "</body>");
        return;
    }

    http_response(fd, "404 Not Found", "text/plain", "Not found.\n");
}

static void *dashboard_main(void *unused)
{
    int s = -1;
    int ever_ready = 0;
    unsigned startup_failures = 0;
    (void)unused;

    for (;;) {
        int one = 1;
        struct sockaddr_in addr;

        atomic_store(&dashboard_started, 0);
        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            int saved_errno = errno;
            startup_failures++;
            if (!ever_ready && startup_failures >= 5U) {
                ps5_log("ERROR", "DASHBOARD_STARTUP_FAILED stage=socket attempts=%u errno=%d %s",
                        startup_failures, saved_errno, strerror(saved_errno));
                atomic_store(&dashboard_started, -1);
                return NULL;
            }
            ps5_log("ERROR", "DASHBOARD_SOCKET_FAILED errno=%d %s retry=%us",
                    saved_errno, strerror(saved_errno), DASHBOARD_RETRY_SECONDS);
            sleep(DASHBOARD_RETRY_SECONDS);
            continue;
        }
        (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(DASHBOARD_PORT);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(s, 4) != 0) {
            int saved_errno = errno;
            startup_failures++;
            ps5_log("ERROR", "DASHBOARD_LISTENER_CREATE_FAILED port=%d errno=%d %s retry=%us",
                    DASHBOARD_PORT, saved_errno, strerror(saved_errno), DASHBOARD_RETRY_SECONDS);
            (void)close(s);
            s = -1;
            if (!ever_ready && startup_failures >= 5U) {
                ps5_log("ERROR", "DASHBOARD_STARTUP_FAILED stage=bind-listen attempts=%u errno=%d %s",
                        startup_failures, saved_errno, strerror(saved_errno));
                atomic_store(&dashboard_started, -1);
                return NULL;
            }
            sleep(DASHBOARD_RETRY_SECONDS);
            continue;
        }

        ever_ready = 1;
        startup_failures = 0;
        atomic_store(&dashboard_started, 1);
        {
            char lan_ip[64] = "";
            (void)tsp5_dashboard_get_lan_ipv4(lan_ip, sizeof(lan_ip));
            ps5_log("INFO", "DASHBOARD_READY address=0.0.0.0 port=%d lan=%s",
                    DASHBOARD_PORT, lan_ip[0] ? lan_ip : "unknown");
        }

        for (;;) {
            int c = accept(s, NULL, NULL);
            if (c < 0) {
                int saved_errno = errno;
                if (saved_errno == EINTR) {
                    continue;
                }
                ps5_log("ERROR", "DASHBOARD_ACCEPT_FAILED errno=%d %s action=recreate-listener",
                        saved_errno, strerror(saved_errno));
                break;
            }
            handle_client(c);
            (void)close(c);
        }

        atomic_store(&dashboard_started, 0);
        (void)close(s);
        s = -1;
        sleep(DASHBOARD_RETRY_SECONDS);
    }
}

int tsp5_dashboard_start(void)
{
    int rc;
    atomic_store(&dashboard_started, 0);
    atomic_store(&register_requested, 0);
    atomic_store(&lifecycle_command, 0);
    if (reset_pending()) {
        set_state("Reset Requested", "", "Restart the PS5 before registering again.");
    } else if (tsp5_dashboard_identity_present()) {
        set_state("Restoring Identity", "", "");
    } else {
        set_state("Not Registered", "", "");
    }

    rc = pthread_create(&dashboard_thread, NULL, dashboard_main, NULL);
    if (rc != 0) {
        atomic_store(&dashboard_started, -1);
        ps5_log("ERROR", "DASHBOARD_THREAD_FAILED rc=%d", rc);
        return -1;
    }
    (void)pthread_detach(dashboard_thread);
    return 0;
}

int tsp5_dashboard_wait_for_registration(void)
{
    while (!tsp5_dashboard_identity_present()) {
        if (atomic_load(&dashboard_started) < 0) {
            return -1;
        }
        if (atomic_load(&register_requested) != 0) {
            return 0;
        }
        sleep(1);
    }
    return 0;
}
