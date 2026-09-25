#ifndef COMPANION_NET_H
#define COMPANION_NET_H
#include <stdbool.h>
bool companion_net_ingest(char *body);
bool companion_net_start(void);
bool companion_net_set_bridge(const char *ip);
#endif
