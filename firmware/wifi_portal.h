#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H
#include <stdbool.h>
bool wifi_portal_start(void);
void wifi_portal_wait_connected(void);
void wifi_portal_companion_ready(bool ready);
#endif

