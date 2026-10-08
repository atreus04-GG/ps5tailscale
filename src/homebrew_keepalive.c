#include "homebrew_keepalive.h"

#include <stdio.h>
#include <string.h>

#define KEEPMAIN_STATUS_PATH "/data/tailscale/keepmain-injector.status"
#define SHELLCORE_STATUS_PATH "/data/tailscale/keepmain-shellcore-injector.status"
#define KEEPMAIN_CONTROL_PATH "/data/tailscale/keepmain.control"

static int status_file_ok(const char *path)
{
    char status[96];
    FILE *f = fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        return 0;
    }
    n = fread(status, 1, sizeof(status) - 1, f);
    (void)fclose(f);
    status[n] = '\0';
    return strncmp(status, "ok:", 3) == 0;
}

static int shellcore_control_requested_active(void)
{
    char value[8];
    FILE *f = fopen(KEEPMAIN_CONTROL_PATH, "rb");
    size_t n;

    if (f == NULL) {
        return 0;
    }
    n = fread(value, 1, sizeof(value) - 1, f);
    (void)fclose(f);
    value[n] = '\0';
    return value[0] == '1';
}

void tsp5_homebrew_keepalive_status(tsp5_homebrew_keepalive_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    out->keepmain_resident_initialized = status_file_ok(KEEPMAIN_STATUS_PATH);
    out->shellcore_companion_initialized = status_file_ok(SHELLCORE_STATUS_PATH);
    out->shellcore_requested_active = shellcore_control_requested_active();
}
