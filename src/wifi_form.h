#ifndef WIFI_FORM_H
#define WIFI_FORM_H
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    char ssid[33];
    char password[64];
    char token[33];
    bool wpa3;
} wifi_form;
bool wifi_form_parse(const char *body, size_t length, wifi_form *form);
void wifi_form_clear(void *data, size_t length);
#endif
