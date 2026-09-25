#include "companion_ui.h"
#include "display_status.h"
#include "board.h"
#include "display_support.h"
#include "fsl_lcdifv2.h"
#include "fsl_iomuxc.h"
#include "fsl_gpio.h"
#include "fsl_gt911.h"
#include "fsl_soc_src.h"
#include "fsl_debug_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "setup_wifi_qr.h"
#include <stdio.h>
#include <string.h>

#define W 720
#define H 1280
#define FRAME_BYTES (W * H / 2)
#define DASHBOARD_MS 18000U
static const uint32_t palette[16] = {
    0xff071322, 0xff12374b, 0xff1b748b, 0xffc4fcf3,
    0xff0f2b44, 0xffffffff, 0xff5be4de, 0xff48f1b9,
    0xffaae8e1, 0xff85bfcc, 0xff2e5b6f, 0xff548b99,
    0xff58babb, 0xffa6c8cf, 0xff16545b, 0xff000000
};
static uint8_t rgb(unsigned r, unsigned g, unsigned b)
{
    unsigned best = 0, distance = ~0U;
    for (unsigned i = 0; i < 16; ++i) {
        int dr = (int)r - (int)((palette[i] >> 16) & 255);
        int dg = (int)g - (int)((palette[i] >> 8) & 255);
        int db = (int)b - (int)(palette[i] & 255);
        unsigned d = dr * dr + dg * dg + db * db;
        if (d < distance) { distance = d; best = i; }
    }
    return best;
}
#define RGB(r,g,b) rgb(r,g,b)
/* Two full-panel packed frames fit in on-chip OCRAM. */
static uint8_t frame[2][FRAME_BYTES] __attribute__((section(".framebuffer"), aligned(64)));
static uint8_t *fb = frame[0];
static volatile bool frame_released;
static void frame_done(void *param, void *old_buffer)
{
    (void)param; (void)old_buffer;
    frame_released = true;
}
static void pixel(int x, int y, uint8_t color)
{
    unsigned offset = (y * W + x) / 2;
    unsigned shift = (x & 1) * 4;
    fb[offset] = (fb[offset] & ~(15U << shift)) | (color << shift);
}
static QueueHandle_t events;
static gt911_handle_t touch;
static bool touch_ready;

typedef struct {
    enum { UI_STATUS, UI_SETUP, UI_DATA } kind;
    char status[48];
    char detail[48];
    companion_snapshot_t data;
} ui_event_t;

static const uint8_t letters[][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
};
static const uint8_t digits[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
    {0x62,0x51,0x49,0x49,0x46}, {0x22,0x41,0x49,0x49,0x36},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e}
};

static void rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) pixel(xx, yy, color);
}

static void ellipse(int cx, int cy, int rx, int ry, uint16_t color)
{
    if (rx <= 0 || ry <= 0) return;
    for (int y = -ry; y <= ry; ++y) {
        int yy = cy + y;
        if ((unsigned)yy >= H) continue;
        int64_t threshold = (int64_t)rx * rx * (ry * ry - y * y);
        int64_t denom = (int64_t)ry * ry;
        for (int x = -rx; x <= rx; ++x) {
            int xx = cx + x;
            if ((unsigned)xx < W && (int64_t)x * x * denom <= threshold)
                pixel(xx, yy, color);
        }
    }
}

static void pill(int x, int y, int w, int h, uint16_t color)
{
    rect(x + h / 2, y, w - h, h, color);
    ellipse(x + h / 2, y + h / 2, h / 2, h / 2, color);
    ellipse(x + w - h / 2, y + h / 2, h / 2, h / 2, color);
}

static void label(int x, int y, const char *s, int scale, uint16_t color)
{
    for (; *s && x + 5 * scale < W; ++s, x += 6 * scale) {
        char c = *s;
        if (c >= 'a' && c <= 'z') c -= 32;
        uint8_t mark[5] = {0};
        const uint8_t *glyph = mark;
        if (c >= 'A' && c <= 'Z') glyph = letters[c - 'A'];
        else if (c >= '0' && c <= '9') glyph = digits[c - '0'];
        else if (c == ':') mark[2] = 0x36;
        else if (c == '.') mark[2] = 0x60;
        else if (c == '-') memset(mark, 0x08, sizeof(mark));
        else if (c == '/') for (int i = 0; i < 5; ++i) mark[i] = 1U << (5 - i);
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (glyph[col] & (1U << row)) rect(x + col * scale, y + row * scale, scale, scale, color);
    }
}

static void center(int y, const char *s, int scale, uint16_t color)
{
    int width = (int)strlen(s) * 6 * scale - scale;
    label((W - width) / 2, y, s, scale, color);
}

/* Small local sentence generator: no cloud account or language model required. */
static void funny_message(TickType_t ticks, int top)
{
    static const char *actions[] = {
        "I AM SECRETLY", "CURRENTLY", "VERY BUSY", "TODAY I AM"
    };
    static const char *missions[] = {
        "COUNTING YOUR BLINKS", "DEBUGGING MY NAP", "CHARGING MY CHARM",
        "HUNTING WIFI GHOSTS", "PLANNING SNACK TIME", "REBOOTING MY COURAGE",
        "PRACTICING HUMANING", "DOWNLOADING A HUG", "TICKLING THE PIXELS"
    };
    static uint32_t random = 0x6d696c6fU;
    static uint32_t previous = UINT32_MAX;
    static unsigned action, mission;
    uint32_t slot = (uint32_t)(ticks / pdMS_TO_TICKS(30000));
    if (slot != previous) {
        previous = slot;
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        action = random % 4;
        unsigned next = (random >> 8) % 9;
        mission = next == mission ? (next + 1) % 9 : next;
    }
    pill(60, top, 600, 100, RGB(20, 58, 74));
    center(top + 19, actions[action], 3, RGB(170, 232, 225));
    center(top + 58, missions[mission], 3, RGB(196, 252, 243));
}

static void background(uint8_t tint)
{
    for (int y = 0; y < H; ++y) {
        uint8_t r = (uint8_t)(7 + y * 5 / H);
        uint8_t g = (uint8_t)(15 + y * 11 / H + tint);
        uint8_t b = (uint8_t)(29 + y * 20 / H + tint);
        uint16_t c = RGB(r, g, b);
        memset(fb + y * W / 2, c | (c << 4), W / 2);
    }
    /* A sparse, deterministic constellation keeps the face alive without flicker. */
    for (int i = 0; i < 24; ++i) {
        int x = 28 + (i * 179) % 665;
        int y = 76 + (i * 257) % 1050;
        ellipse(x, y, i % 7 == 0 ? 3 : 1, i % 7 == 0 ? 3 : 1, RGB(46, 91, 111));
    }
}

static void draw_eye(int x, int y, int gaze, int opening)
{
    ellipse(x, y, 165, 185, RGB(17, 57, 75));
    ellipse(x, y, 151, 171, RGB(27, 116, 139));
    ellipse(x, y, 139, opening, RGB(196, 252, 243));
    if (opening > 30) {
        ellipse(x + gaze, y + 12, 67, opening < 67 ? opening - 6 : 67, RGB(15, 43, 68));
        ellipse(x + gaze + 20, y - 18, 22, 22, RGB(255, 255, 255));
        ellipse(x + gaze - 20, y + 47, 9, 9, RGB(91, 228, 222));
    }
}

static void draw_face(TickType_t ticks, const companion_snapshot_t *data, TickType_t received)
{
    uint32_t ms = (uint32_t)(ticks * portTICK_PERIOD_MS);
    background(0);
    pill(60, 55, 600, 70, RGB(20, 58, 74));
    ellipse(105, 90, 9, 9, RGB(72, 241, 185));
    label(135, 76, "MILO  IS HERE", 3, RGB(170, 232, 225));
    const char *greeting = "HELLO HUMAN";
    if (data->valid) {
        int hour = ((data->time[0] - '0') * 10 + data->time[1] - '0' +
                    (int)((ticks - received) * portTICK_PERIOD_MS / 3600000U)) % 24;
        greeting = hour < 11 ? "GOOD MORNING" : hour < 17 ? "GOOD AFTERNOON" : "GOOD EVENING";
    }
    center(212, greeting, 4, RGB(133, 191, 204));
    int gaze_phase = (int)(ms % 5400U);
    int gaze = gaze_phase < 2700 ? -20 + gaze_phase * 40 / 2700
                                   : 20 - (gaze_phase - 2700) * 40 / 2700;
    int phase = (int)(ms % 6900U);
    int opening = 150;
    if (phase > 6400) {
        int blink = phase - 6400;
        opening = 8 + (blink < 250 ? 142 * (250 - blink) / 250 : 142 * (blink - 250) / 250);
    }
    draw_eye(204, 572, gaze, ((ms / 6900U) % 4U == 3U) ? 150 : opening);
    draw_eye(516, 572, gaze, opening);
    ellipse(360, 785, 47, 10, RGB(92, 218, 202));
    pill(168, 974, 384, 72, RGB(21, 70, 82));
    center(994, "TAP TO EXPLORE", 3, RGB(171, 246, 229));
    funny_message(ticks, 1150);
}

static const char *weather_name(uint8_t code)
{
    if (code == 0) return "CLEAR SKY";
    if (code <= 3) return "PARTLY CLOUDY";
    if (code <= 48) return "FOGGY";
    if (code <= 67) return "RAINY";
    if (code <= 77) return "SNOWY";
    if (code <= 82) return "SHOWERS";
    if (code <= 86) return "SNOW SHOWERS";
    if (code >= 95) return "THUNDERSTORM";
    return "MIXED WEATHER";
}

static void draw_dashboard(const companion_snapshot_t *data, TickType_t received, TickType_t now)
{
    background(6);
    label(64, 76, "MILO / TODAY", 4, RGB(180, 252, 237));
    pill(60, 180, 600, 330, RGB(18, 55, 75));
    center(222, "LOCAL TIME", 3, RGB(88, 186, 187));
    if (data->valid) {
        int hh = (data->time[0]-'0')*10 + data->time[1]-'0';
        int mm = (data->time[3]-'0')*10 + data->time[4]-'0';
        unsigned elapsed = (unsigned)((now - received) * portTICK_PERIOD_MS / 60000U);
        hh = (hh + (mm + (int)elapsed) / 60) % 24;
        mm = (mm + (int)elapsed) % 60;
        char clock_text[8];
        snprintf(clock_text, sizeof(clock_text), "%02d:%02d", hh, mm);
        center(295, clock_text, 13, RGB(244, 255, 252));
        center(452, data->date, 3, RGB(139, 195, 204));
    } else {
        center(315, "CLOCK WAITING", 5, RGB(244, 255, 252));
        center(448, "CONNECT USB FOR SYNC", 2, RGB(139, 195, 204));
    }
    pill(60, 550, 600, 430, RGB(18, 55, 75));
    center(602, "FORECAST / TODAY", 3, RGB(88, 186, 187));
    if (data->weather_valid) {
        char temp[22], range[30];
        snprintf(temp, sizeof(temp), "%d C", data->temperature);
        snprintf(range, sizeof(range), "HIGH %d  LOW %d", data->high, data->low);
        center(685, temp, 9, RGB(245, 254, 245));
        center(795, weather_name(data->weather_code), 3, RGB(151, 239, 224));
        center(852, range, 3, RGB(166, 200, 207));
        center(904, data->city, 3, RGB(106, 172, 181));
        center(946, "IP LOCATION ESTIMATE", 2, RGB(106, 172, 181));
    } else {
        center(735, "WEATHER OFFLINE", 4, RGB(245, 254, 245));
        center(826, "CHECK USB AND INTERNET", 2, RGB(151, 239, 224));
    }
    pill(145, 1070, 430, 72, RGB(22, 84, 91));
    center(1091, "TAP TO SEE MILO", 3, RGB(190, 245, 226));
    funny_message(now, 1150);
}

static void draw_status(const char *status, const char *detail, bool setup)
{
    background(2);
    center(160, "MILO IS WAKING UP", 4, RGB(190, 250, 233));
    if (setup) {
        const int scale = 10;
        int extent = (SETUP_QR_SIZE + 8) * scale;
        int left = (W - extent) / 2, top = 345;
        rect(left, top, extent, extent, RGB(255, 255, 255));
        for (int y = 0; y < SETUP_QR_SIZE; ++y)
            for (int x = 0; x < SETUP_QR_SIZE; ++x)
                if (setup_qr_rows[y][x / 8] & (0x80U >> (x % 8)))
                    rect(left + (x + 4) * scale, top + (y + 4) * scale,
                         scale, scale, RGB(4, 22, 30));
        center(260, "SCAN TO JOIN MILO", 3, RGB(241, 255, 251));
        center(920, "OPEN 192.168.4.1", 3, RGB(132, 205, 204));
        center(1010, "SET UP YOUR WI-FI", 3, RGB(132, 205, 204));
        center(1110, status, 2, RGB(170, 232, 225));
        center(1160, detail, 2, RGB(170, 232, 225));
    } else {
        draw_eye(204, 475, 0, 136);
        draw_eye(516, 475, 0, 136);
        center(780, status, 2, RGB(241, 255, 251));
        center(852, detail, 3, RGB(132, 205, 204));
    }
}

static void delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1); }
static void touch_reset(bool up)
{
    GPIO_PinWrite(BOARD_MIPI_PANEL_TOUCH_RST_GPIO, BOARD_MIPI_PANEL_TOUCH_RST_PIN, up ? 1 : 0);
}
static void touch_int(gt911_int_pin_mode_t mode)
{
    if (mode == kGT911_IntPinInput)
        BOARD_MIPI_PANEL_TOUCH_INT_GPIO->GDIR &= ~(1UL << BOARD_MIPI_PANEL_TOUCH_INT_PIN);
    else {
        GPIO_PinWrite(BOARD_MIPI_PANEL_TOUCH_INT_GPIO, BOARD_MIPI_PANEL_TOUCH_INT_PIN,
                      mode == kGT911_IntPinPullDown ? 0 : 1);
        BOARD_MIPI_PANEL_TOUCH_INT_GPIO->GDIR |= 1UL << BOARD_MIPI_PANEL_TOUCH_INT_PIN;
    }
}

static void init_touch(void)
{
    CLOCK_EnableClock(kCLOCK_Iomuxc_Lpsr);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_00_GPIO8_IO31, 1U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_01_GPIO9_IO00, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_LPSR_04_LPI2C5_SDA, 1U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_LPSR_05_LPI2C5_SCL, 1U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_LPSR_04_LPI2C5_SDA, 0x20U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_LPSR_05_LPI2C5_SCL, 0x20U);
    BOARD_MIPIPanelTouch_I2C_Init();
    const gpio_pin_config_t pin = {kGPIO_DigitalOutput, 0, kGPIO_NoIntmode};
    GPIO_PinInit(BOARD_MIPI_PANEL_TOUCH_INT_GPIO, BOARD_MIPI_PANEL_TOUCH_INT_PIN, &pin);
    GPIO_PinInit(BOARD_MIPI_PANEL_TOUCH_RST_GPIO, BOARD_MIPI_PANEL_TOUCH_RST_PIN, &pin);
    const gt911_config_t config = {
        .I2C_SendFunc = BOARD_MIPIPanelTouch_I2C_Send,
        .I2C_ReceiveFunc = BOARD_MIPIPanelTouch_I2C_Receive,
        .pullResetPinFunc = touch_reset,
        .intPinFunc = touch_int,
        .timeDelayMsFunc = delay_ms,
        .touchPointNum = 1,
        .i2cAddrMode = kGT911_I2cAddrMode0,
        .intTrigMode = kGT911_IntRisingEdge,
    };
    touch_ready = GT911_Init(&touch, &config) == kStatus_Success;
    PRINTF("Touch: GT911 %s.\r\n", touch_ready ? "ready" : "initialization failed");
}

static void screen_task(void *unused)
{
    (void)unused;
    SRC_AssertSliceSoftwareReset(SRC, kSRC_DisplaySlice);
    TickType_t begin = xTaskGetTickCount();
    while (SRC_GetSliceResetState(SRC, kSRC_DisplaySlice) == kSRC_SliceResetInProcess) {
        if (xTaskGetTickCount() - begin > pdMS_TO_TICKS(1000)) goto failed;
        vTaskDelay(1);
    }
    CLOCK_EnableClock(kCLOCK_Iomuxc);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_02_GPIO9_IO01, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_30_GPIO9_IO29, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_DISP_B2_15_GPIO11_IO16, 0U);
    if (BOARD_PrepareDisplayController() != kStatus_Success) goto failed;
    if (g_dc.ops->init(&g_dc) != kStatus_Success) goto failed;
    dc_fb_info_t info = {
        .startX = 0, .startY = 0, .width = W, .height = H,
        .strideBytes = W, .pixelFormat = kVIDEO_PixelFormatLUT8
    };
    if (g_dc.ops->setLayerConfig(&g_dc, 0, &info) != kStatus_Success) goto failed;
    const lcdifv2_buffer_config_t packed = {
        .strideBytes = W / 2, .pixelFormat = kLCDIFV2_PixelFormatIndex4BPP
    };
    LCDIFV2_SetLayerBufferConfig(LCDIFV2, 0, &packed);
    g_dc.ops->setCallback(&g_dc, 0, frame_done, NULL);
    if (LCDIFV2_SetLut(LCDIFV2, 0, palette, 16, false) != kStatus_Success) goto failed;
    draw_status("STARTING", "PLEASE WAIT", false);
    SCB_CleanDCache_by_Addr((uint32_t *)fb, FRAME_BYTES);
    __DSB();
    if (g_dc.ops->setFrameBuffer(&g_dc, 0, fb) != kStatus_Success) goto failed;
    if (g_dc.ops->enableLayer(&g_dc, 0) != kStatus_Success) goto failed;
    fb = frame[1];
    init_touch();
    PRINTF("Display: RK055MHD091 full-screen OCRAM companion ready.\r\n");
    bool setup = true, online = false, dashboard = false, pressed = false;
    char status[48] = "STARTING WI-FI", detail[48] = "PLEASE WAIT";
    companion_snapshot_t snapshot = {0};
    TickType_t received = 0, dashboard_at = 0;
    for (;;) {
        ui_event_t event;
        while (xQueueReceive(events, &event, 0) == pdTRUE) {
            if (event.kind == UI_SETUP) { setup = true; online = false; }
            else if (event.kind == UI_STATUS) {
                snprintf(status, sizeof(status), "%s", event.status);
                snprintf(detail, sizeof(detail), "%s", event.detail);
                if (!strcmp(status, "HOME WI-FI CONNECTED")) online = true;
                if (strstr(status, "FAILED") || strstr(status, "DISCONNECTED")) {
                    online = false;
                    setup = true;
                }
            } else if (event.kind == UI_DATA) {
                snapshot = event.data;
                received = xTaskGetTickCount();
                if (online && snapshot.weather_valid) setup = false;
            }
        }
        if (touch_ready) {
            int x, y;
            bool down = GT911_GetSingleTouch(&touch, &x, &y) == kStatus_Success;
            if (down && !pressed) {
                dashboard = !dashboard;
                dashboard_at = xTaskGetTickCount();
            }
            pressed = down;
        }
        TickType_t now = xTaskGetTickCount();
        if (dashboard && (now - dashboard_at) * portTICK_PERIOD_MS >= DASHBOARD_MS) dashboard = false;
        if (dashboard) draw_dashboard(&snapshot, received, now);
        else if (setup) draw_status(status, detail, true);
        else {
            draw_face(now, &snapshot, received);
            if (!online) center(1099, status, 2, RGB(150, 206, 211));
        }
        SCB_CleanDCache_by_Addr((uint32_t *)fb, FRAME_BYTES);
        __DSB();
        frame_released = false;
        if (g_dc.ops->setFrameBuffer(&g_dc, 0, fb) != kStatus_Success) goto failed;
        /* Never modify a frame until the controller retires it at vertical blank. */
        TickType_t swap_start = xTaskGetTickCount();
        while (!frame_released) {
            if (xTaskGetTickCount() - swap_start > pdMS_TO_TICKS(1000)) goto failed;
            vTaskDelay(1);
        }
        fb = fb == frame[0] ? frame[1] : frame[0];
        vTaskDelay(pdMS_TO_TICKS(dashboard || setup ? 100 : 30));
    }
failed:
    PRINTF("Display: companion initialization failed.\r\n");
    vTaskDelete(NULL);
}

bool companion_ui_start(void)
{
    events = xQueueCreate(4, sizeof(ui_event_t));
    if (!events) return false;
    if (xTaskCreate(screen_task, "milo-screen", 2048, NULL, 2, NULL) == pdPASS) return true;
    vQueueDelete(events);
    events = NULL;
    return false;
}

void companion_ui_update(const companion_snapshot_t *snapshot)
{
    if (!events || !snapshot) return;
    ui_event_t event = {.kind = UI_DATA, .data = *snapshot};
    xQueueSend(events, &event, 0);
}

void display_status_set(const char *status, const char *detail)
{
    if (!events) return;
    ui_event_t event = {.kind = UI_STATUS};
    snprintf(event.status, sizeof(event.status), "%s", status);
    snprintf(event.detail, sizeof(event.detail), "%s", detail);
    xQueueSend(events, &event, 0);
}

void display_status_show_setup(void)
{
    if (!events) return;
    ui_event_t event = {.kind = UI_SETUP};
    xQueueSend(events, &event, 0);
}
