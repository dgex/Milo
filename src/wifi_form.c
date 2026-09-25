#include "wifi_form.h"
#include <string.h>

void wifi_form_clear(void *data, size_t length)
{
    volatile unsigned char *p = data;
    while (length--) *p++ = 0;
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool decode(const char *s, size_t n, char *out, size_t capacity)
{
    size_t used = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '+') c = ' ';
        else if (c == '%') {
            if (i + 2 >= n || hex(s[i + 1]) < 0 || hex(s[i + 2]) < 0) return false;
            c = (unsigned char)(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        }
        if (c < 32 || c == 127 || used + 1 >= capacity) return false;
        out[used++] = (char)c;
    }
    out[used] = 0;
    return true;
}

bool wifi_form_parse(const char *body, size_t length, wifi_form *form)
{
    unsigned seen = 0;
    char security[8] = {0};
    if (!form) return false;
    memset(form, 0, sizeof(*form));
    if (!body || !length || length > 512) return false;
    size_t pos = 0;
    while (pos < length) {
        size_t end = pos, equals = pos;
        while (end < length && body[end] != '&') ++end;
        while (equals < end && body[equals] != '=') ++equals;
        if (equals == end) goto invalid;
        char key[12];
        if (!decode(body + pos, equals - pos, key, sizeof(key))) goto invalid;
        char *dst;
        size_t cap;
        unsigned bit;
        if (!strcmp(key, "ssid")) { dst = form->ssid; cap = sizeof(form->ssid); bit = 1; }
        else if (!strcmp(key, "password")) { dst = form->password; cap = sizeof(form->password); bit = 2; }
        else if (!strcmp(key, "token")) { dst = form->token; cap = sizeof(form->token); bit = 4; }
        else if (!strcmp(key, "security")) { dst = security; cap = sizeof(security); bit = 8; }
        else goto invalid;
        if ((seen & bit) || !decode(body + equals + 1, end - equals - 1, dst, cap)) goto invalid;
        seen |= bit;
        pos = end + 1;
        if (end < length && pos == length) goto invalid;
    }
    if (seen != 15 || !form->ssid[0] || strlen(form->token) != 32) goto invalid;
    for (size_t i = 0; i < 32; ++i) if (hex(form->token[i]) < 0) goto invalid;
    if (!strcmp(security, "open")) {
        if (form->password[0]) goto invalid;
    } else {
        if (strcmp(security, "wpa2") && strcmp(security, "wpa3")) goto invalid;
        if (strlen(form->password) < 8) goto invalid;
        for (size_t i = 0; form->password[i]; ++i)
            if ((unsigned char)form->password[i] > 126) goto invalid;
        form->wpa3 = !strcmp(security, "wpa3");
    }
    return true;
invalid:
    wifi_form_clear(form, sizeof(*form));
    return false;
}
