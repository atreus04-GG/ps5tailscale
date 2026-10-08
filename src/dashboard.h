#ifndef TSP5_DASHBOARD_H
#define TSP5_DASHBOARD_H

#include <stddef.h>

int tsp5_dashboard_start(void);
int tsp5_dashboard_identity_present(void);
int tsp5_dashboard_mark_registered(void);
int tsp5_dashboard_request_registration_reset(void);
int tsp5_dashboard_authkey_present(void);
int tsp5_dashboard_wait_for_registration(void);
int tsp5_dashboard_take_lifecycle_command(void);
int tsp5_dashboard_get_lan_ipv4(char *out, size_t out_size);
void tsp5_dashboard_set_connecting(void);
void tsp5_dashboard_set_connected(const char *ips);
void tsp5_dashboard_set_disconnected(void);
void tsp5_dashboard_set_error(const char *message);

#endif
