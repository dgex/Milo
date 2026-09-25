#include "wifi_portal.h"
#include "display_status.h"
#include "wifi_form.h"
#include "portal_assets.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "httpsrv.h"
#include "wpl.h"
#include "wlan.h"
#include "fsl_debug_console.h"
#include <stdio.h>
#include <string.h>

static QueueHandle_t requests;
static SemaphoreHandle_t lock, connected;
static enum { READY, CONNECTING, FAILED, CONNECTED } state;
static char address[16];
static bool companion_ready;
static const HTTPSRV_FS_DIR_ENTRY files[] = {
    {"/index.html", HTTPSRV_FS_FLAG_INDEX, (unsigned char *)portal_html, sizeof(portal_html) - 1},
    {"/qr.js", 0, (unsigned char *)portal_qr_js, sizeof(portal_qr_js) - 1},
    {NULL, 0, NULL, 0}
};

static int respond(HTTPSRV_CGI_REQ_STRUCT *request, unsigned code, char *body)
{
    HTTPSRV_CGI_RES_STRUCT response = {0};
    response.ses_handle = request->ses_handle;
    response.status_code = code;
    response.content_type = HTTPSRV_CONTENT_TYPE_PLAIN;
    response.data = body;
    response.data_length = (uint32_t)strlen(body);
    response.content_length = (int32_t)response.data_length;
    HTTPSRV_cgi_write(&response);
    return (int)response.data_length;
}

static int status_request(HTTPSRV_CGI_REQ_STRUCT *request)
{
    static unsigned requests_seen;
    unsigned seen = ++requests_seen;
    if (seen <= 5 || seen % 20 == 0)
        PRINTF("Web setup: status request %u.\r\n", seen);
    if (request->request_method != HTTPSRV_REQ_GET)
        return respond(request, 405, "{}");
    char body[128];
    static const char *names[] = {"ready", "connecting", "failed", "connected"};
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(body, sizeof(body), "{\"state\":\"%s\",\"ip\":\"%s\",\"ready\":%s}",
             names[state], address, companion_ready ? "true" : "false");
    xSemaphoreGive(lock);
    return respond(request, 200, body);
}

static int join_request(HTTPSRV_CGI_REQ_STRUCT *request)
{
    PRINTF("Web setup: join request method=%d type=%d length=%u.\r\n",
           (int)request->request_method, (int)request->content_type,
           (unsigned)request->content_length);
    if (request->request_method != HTTPSRV_REQ_POST) return respond(request, 405, "{}");
    if (request->content_type != HTTPSRV_CONTENT_TYPE_FORMURLENC) return respond(request, 415, "{}");
    if (!request->content_length || request->content_length > 512) return respond(request, 413, "{}");
    /* Provision only from the setup subnet; token prevents cross-origin form submissions. */
    if (!request->remote_addr || strncmp(request->remote_addr, "192.168.4.", 10))
        return respond(request, 403, "{}");
    char body[513] = {0};
    wifi_form form;
    size_t used = 0;
    while (used < request->content_length) {
        uint32_t n = HTTPSRV_cgi_read(request->ses_handle, body + used, request->content_length - (uint32_t)used);
        if (!n || n > request->content_length - used) break;
        used += n;
    }
    bool valid = used == request->content_length && wifi_form_parse(body, used, &form);
    wifi_form_clear(body, sizeof(body));
    if (!valid) { wifi_form_clear(&form, sizeof(form)); return respond(request, 400, "{}"); }
    if (strcmp(form.token, PORTAL_TOKEN)) {
        wifi_form_clear(&form, sizeof(form));
        return respond(request, 403, "{}");
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    bool accepted = false;
    if (state == READY || state == FAILED) {
        accepted = xQueueSend(requests, &form, 0) == pdTRUE;
        if (accepted) state = CONNECTING;
    }
    xSemaphoreGive(lock);
    wifi_form_clear(&form, sizeof(form));
    PRINTF("Web setup: request %s.\r\n", accepted ? "accepted" : "busy");
    return respond(request, accepted ? 202 : 409, "{}");
}

static const HTTPSRV_CGI_LINK_STRUCT handlers[] = {
    {"join", join_request}, {"status", status_request}, {NULL, NULL}
};

static wpl_ret_t add_home_network(const wifi_form *form)
{
    if (!form->password[0])
        return WPL_AddNetworkWithSecurity(form->ssid, form->password, "companion", WPL_SECURITY_WILDCARD);
    if (form->wpa3)
        return WPL_AddNetworkWithSecurity(form->ssid, form->password, "companion", WPL_SECURITY_WPA3_SAE);

    /* WPL's wildcard profile requires PMF, which excludes many WPA2 routers. */
    struct wlan_network network = {0};
    size_t ssid_len = strlen(form->ssid);
    size_t password_len = strlen(form->password);
    memcpy(network.name, "companion", sizeof("companion"));
    memcpy(network.ssid, form->ssid, ssid_len);
    network.security.type = WLAN_SECURITY_WPA2;
    network.security.key_mgmt = WLAN_KEY_MGMT_PSK;
    network.security.mfpc = 0;
    network.security.mfpr = 0;
    network.security.psk_len = (uint8_t)password_len;
    memcpy(network.security.psk, form->password, password_len);
    network.ip.ipv4.addr_type = ADDR_TYPE_DHCP;
    network.ssid_specific = 1;
    int result = wlan_add_network(&network);
    wifi_form_clear(&network, sizeof(network));
    return result == WM_SUCCESS ? WPLRET_SUCCESS : WPLRET_FAIL;
}

static void provision_task(void *unused)
{
    (void)unused;
    for (;;) {
        wifi_form form;
        xQueueReceive(requests, &form, portMAX_DELAY);
        display_status_set("CONNECTING TO HOME WI-FI", "PLEASE WAIT");
        PRINTF("Web setup: adding home network profile...\r\n");
        wpl_ret_t result = add_home_network(&form);
        PRINTF("Web setup: add profile result: %d\r\n", result);
        wifi_form_clear(&form, sizeof(form));
        /* Clear the queue's retained copy of credentials as well. */
        xQueueSend(requests, &form, 0);
        xQueueReceive(requests, &form, 0);
        bool ap_stopped = false;
        if (result == WPLRET_SUCCESS) {
            /* Let the HTTP response leave before switching the radio to STA mode. */
            vTaskDelay(pdMS_TO_TICKS(1500));
            PRINTF("Web setup: stopping setup AP before station join...\r\n");
            result = WPL_Stop_AP();
            PRINTF("Web setup: stop AP result: %d\r\n", result);
            ap_stopped = result == WPLRET_SUCCESS;
        }
        if (result == WPLRET_SUCCESS) {
            PRINTF("Web setup: joining home network...\r\n");
            result = WPL_Join("companion");
            PRINTF("Web setup: join result: %d\r\n", result);
        }
        char ip[16] = {0};
        if (result == WPLRET_SUCCESS) {
            result = WPL_GetIP(ip, 1);
            PRINTF("Web setup: IP result: %d\r\n", result);
        }
        if (result != WPLRET_SUCCESS) {
            WPL_Leave();
            WPL_RemoveNetwork("companion");
            if (ap_stopped) {
                PRINTF("Web setup: restarting setup AP...\r\n");
                wpl_ret_t restart = WPL_Start_AP("Milo-Setup", PORTAL_AP_PASSWORD, 6);
                PRINTF("Web setup: restart AP result: %d\r\n", restart);
            }
            xSemaphoreTake(lock, portMAX_DELAY);
            state = FAILED;
            xSemaphoreGive(lock);
            PRINTF("Web setup: connection failed (%d); retry in browser.\r\n", result);
            display_status_show_setup();
            continue;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        memcpy(address, ip, sizeof(address));
        state = CONNECTED;
        xSemaphoreGive(lock);
        PRINTF("Web setup: connected. Board IP: %s\r\n", ip);
        display_status_set("HOME WI-FI CONNECTED", ip);
        xSemaphoreGive(connected);
        vTaskDelete(NULL);
        return;
    }
}

bool wifi_portal_start(void)
{
    requests = xQueueCreate(1, sizeof(wifi_form));
    lock = xSemaphoreCreateMutex();
    connected = xSemaphoreCreateBinary();
    configASSERT(requests && lock && connected);
    PRINTF("Starting setup access point... free heap=%u\r\n", (unsigned)xPortGetFreeHeapSize());
    wpl_ret_t ap_status = WPL_Start_AP("Milo-Setup", PORTAL_AP_PASSWORD, 6);
    PRINTF("Setup AP result: %d\r\n", ap_status);
    if (ap_status != WPLRET_SUCCESS) return false;
    HTTPSRV_FS_init(files);
    HTTPSRV_PARAM_STRUCT params = {0};
    params.root_dir = "";
    params.index_page = "/index.html";
    params.cgi_lnk_tbl = handlers;
    params.max_ses = 4;
    params.task_prio = 1;
    PRINTF("Starting HTTP server...\r\n");
    if (!HTTPSRV_init(&params)) { PRINTF("HTTP server init failed.\r\n"); WPL_Stop_AP(); return false; }
    BaseType_t ok = xTaskCreate(provision_task, "wifi-setup", 2048, NULL, 1, NULL);
    configASSERT(ok == pdPASS);
    display_status_show_setup();
    PRINTF("\r\nSetup Wi-Fi: Milo-Setup\r\nSetup password: %s\r\nSetup page: http://%s/\r\n",
           PORTAL_AP_PASSWORD, WPL_WIFI_AP_IP_ADDR);
    return true;
}

void wifi_portal_wait_connected(void) { xSemaphoreTake(connected, portMAX_DELAY); }
void wifi_portal_companion_ready(bool ready)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    companion_ready = ready;
    xSemaphoreGive(lock);
}


