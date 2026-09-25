#include "companion_net.h"
#include "companion_ui.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "fsl_debug_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define BRIDGE_PORT 8765
static char bridge_ip[16];

bool companion_net_set_bridge(const char *ip)
{
    if (!ip || strlen(ip) > 15 || inet_addr(ip) == INADDR_NONE) return false;
    taskENTER_CRITICAL();
    snprintf(bridge_ip, sizeof(bridge_ip), "%s", ip);
    taskEXIT_CRITICAL();
    return true;
}

static bool field(const char *body, const char *key, char *out, size_t capacity)
{
    size_t n = strlen(key);
    for (const char *p = body; *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : p + strlen(p)) {
        if (!strncmp(p, key, n) && p[n] == '=') {
            p += n + 1;
            size_t length = strcspn(p, "\r\n");
            if (length == 0 || length >= capacity) return false;
            memcpy(out, p, length);
            out[length] = 0;
            return true;
        }
    }
    return false;
}

static bool number(const char *body, const char *key, int16_t *out)
{
    char value[12];
    if (!field(body, key, value, sizeof(value))) return false;
    char *end;
    long n = strtol(value, &end, 10);
    if (*end || n < -100 || n > 200) return false;
    *out = (int16_t)n;
    return true;
}

static bool parse(const char *body, companion_snapshot_t *snapshot)
{
    int16_t code;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!field(body, "TIME", snapshot->time, sizeof(snapshot->time)) ||
        !field(body, "DATE", snapshot->date, sizeof(snapshot->date)) ||
        !field(body, "CITY", snapshot->city, sizeof(snapshot->city))) return false;
    if (strlen(snapshot->time) != 5 || snapshot->time[2] != ':' ||
        !isdigit((unsigned char)snapshot->time[0]) || !isdigit((unsigned char)snapshot->time[1]) ||
        !isdigit((unsigned char)snapshot->time[3]) || !isdigit((unsigned char)snapshot->time[4]) ||
        atoi(snapshot->time) > 23 || atoi(snapshot->time + 3) > 59) return false;
    code = 0;
    snapshot->weather_valid = number(body, "TEMP", &snapshot->temperature) &&
        number(body, "HIGH", &snapshot->high) && number(body, "LOW", &snapshot->low) &&
        number(body, "CODE", &code) && code >= 0 && code <= 99;
    snapshot->weather_code = (uint8_t)code;
    snapshot->valid = true;
    return true;
}

bool companion_net_ingest(char *body)
{
    for (char *p = body; *p; ++p) if (*p == '|') *p = '\n';
    companion_snapshot_t snapshot;
    if (!parse(body, &snapshot)) return false;
    companion_ui_update(&snapshot);
    return true;
}

static bool fetch(const char *ip)
{
    int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return false;
    struct timeval timeout = {.tv_sec = 3, .tv_usec = 0};
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(BRIDGE_PORT);
    address.sin_addr.s_addr = inet_addr(ip);
    bool ok = false;
    if (lwip_connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0) {
        static const char request[] = "GET /v1/today HTTP/1.0\r\nHost: milo\r\nConnection: close\r\n\r\n";
        if (lwip_send(fd, request, sizeof(request) - 1, 0) == sizeof(request) - 1) {
            char reply[1024];
            size_t used = 0;
            int n;
            while (used < sizeof(reply) - 1 && (n = lwip_recv(fd, reply + used, sizeof(reply) - 1 - used, 0)) > 0)
                used += (size_t)n;
            reply[used] = 0;
            char *body = strstr(reply, "\r\n\r\n");
            if (body && (!strncmp(reply, "HTTP/1.0 200", 12) || !strncmp(reply, "HTTP/1.1 200", 12))) {
                companion_snapshot_t snapshot;
                if (parse(body + 4, &snapshot)) {
                    companion_ui_update(&snapshot);
                    ok = true;
                }
            }
        }
    }
    lwip_close(fd);
    return ok;
}

static void weather_task(void *unused)
{
    (void)unused;
    for (;;) {
        char ip[16];
        taskENTER_CRITICAL();
        memcpy(ip, bridge_ip, sizeof(ip));
        taskEXIT_CRITICAL();
        if (ip[0] && !fetch(ip)) PRINTF("Bridge: fetch failed at %s:%d.\r\n", ip, BRIDGE_PORT);
        vTaskDelay(pdMS_TO_TICKS(ip[0] ? 60000 : 5000));
    }
}

bool companion_net_start(void)
{
    return xTaskCreate(weather_task, "milo-weather", 2048, NULL, 1, NULL) == pdPASS;
}
