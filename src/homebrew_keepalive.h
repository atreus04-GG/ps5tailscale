#ifndef TSP5_HOMEBREW_KEEPALIVE_H
#define TSP5_HOMEBREW_KEEPALIVE_H

typedef struct tsp5_homebrew_keepalive_status {
    int keepmain_resident_initialized;
    int shellcore_companion_initialized;
    int shellcore_requested_active;
} tsp5_homebrew_keepalive_status_t;

void tsp5_homebrew_keepalive_status(tsp5_homebrew_keepalive_status_t *out);

#endif
