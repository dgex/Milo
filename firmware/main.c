#include "board.h"
#include "app.h"
#include "fsl_debug_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "wpl.h"
#include "wifi_portal.h"
#include "companion_ui.h"
#include "companion_net.h"
#include "display_status.h"
#include <stdio.h>
#include <string.h>

static bool skip_lf;

static bool read_line(char *out, size_t capacity)
{
    size_t used = 0;
    bool overflow = false;
    for (;;) {
        char c;
        if (DbgConsole_TryGetchar(&c) != kStatus_Success) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if (skip_lf && c == '\n') { skip_lf = false; continue; }
        skip_lf = false;
        if (c == '\r' || c == '\n') {
            skip_lf = c == '\r';
            out[used] = 0;
            PRINTF("\r\n");
            return !overflow;
        }
        if (c == '\b' || c == 127) {
            if (used) { --used; PRINTF("\b \b"); }
        } else if ((unsigned char)c >= 32) {
            if (used + 1 < capacity) { out[used++] = c; PUTCHAR(c); }
            else overflow = true;
        }
    }
}

static void link_changed(bool connected)
{
    PRINTF("Wi-Fi link %s.\r\n", connected ? "up" : "lost");
    if (!connected) display_status_set("WI-FI DISCONNECTED", "CHECK NETWORK");
}

static void main_task(void *unused)
{
    (void)unused;
    if (!companion_ui_start()) PRINTF("Companion screen task creation failed.\r\n");
    else display_status_show_setup();
    PRINTF("\r\nMilo companion / RT1170 EVKB / Murata 2EL\r\n");
    PRINTF("Keep the onboard microSD slot empty while M.2 Wi-Fi uses USDHC1.\r\n");
    /* Let the higher-priority display task initialize first. */
    vTaskDelay(pdMS_TO_TICKS(500));
    if (WPL_Init() != WPLRET_SUCCESS || WPL_Start(link_changed) != WPLRET_SUCCESS) {
        display_status_set("WI-FI INIT FAILED", "POWER CYCLE BOARD");
        PRINTF("Wi-Fi initialization failed. Full power cycle may be needed after flashing.\r\n");
        goto commands;
    }
    if (!wifi_portal_start()) {
        display_status_set("SETUP FAILED", "POWER CYCLE BOARD");
        vTaskDelete(NULL);
        return;
    }
    wifi_portal_wait_connected();
    wifi_portal_companion_ready(true);
    companion_net_start();
commands:;
    char ip[16] = {0};

    PRINTF("Enter 'bridge A.B.C.D' to use the LAN clock/weather bridge on port 8765.\r\n");
    PRINTF("Commands: bridge IP, ip, help\r\n");
    char line[256];
    for (;;) {
        PRINTF("milo> ");
        if (!read_line(line, sizeof(line))) { PRINTF("Command too long.\r\n"); continue; }
        if (!strncmp(line, "data ", 5)) {
            PRINTF("Data %s.\r\n", companion_net_ingest(line + 5) ? "updated" : "invalid");
        } else if (!strncmp(line, "bridge ", 7)) {
            PRINTF("Bridge %s.\r\n", companion_net_set_bridge(line + 7) ? "configured" : "IP invalid");
        } else if (!strcmp(line, "ip")) {
            if (WPL_GetIP(ip, 1) == WPLRET_SUCCESS) PRINTF("Companion IP: %s\r\n", ip);
            else PRINTF("No IPv4 address.\r\n");
        } else if (!strcmp(line, "help")) PRINTF("bridge A.B.C.D / ip / help\r\n");
    }
}

void vApplicationMallocFailedHook(void)
{
    PRINTF("FATAL: FreeRTOS heap exhausted.\r\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    PRINTF("FATAL: task stack overflow: %s\r\n", name);
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

int main(void)
{
    BOARD_InitHardware();
    /* BOARD_ConfigMPU maps SDRAM write-through for LCD DMA. */
    if (xTaskCreate(main_task, "milo-main", 2048, NULL, 1, NULL) != pdPASS) for (;;) {}
    vTaskStartScheduler();
    for (;;) {}
}

