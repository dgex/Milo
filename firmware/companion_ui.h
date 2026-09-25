#ifndef COMPANION_UI_H
#define COMPANION_UI_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;
    bool weather_valid;
    char time[6];
    char date[24];
    char city[24];
    int16_t temperature;
    int16_t high;
    int16_t low;
    uint8_t weather_code;
} companion_snapshot_t;

bool companion_ui_start(void);
void companion_ui_update(const companion_snapshot_t *snapshot);
#endif
