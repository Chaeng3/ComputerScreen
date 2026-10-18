#include "form.h"
#include <string.h>
#include <ctype.h>

static int hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool form_get(const char *body, const char *name, char *out, size_t capacity)
{
    if (!capacity) return false;
    out[0] = 0;
    size_t name_len = strlen(name);
    bool found = false;
    const char *p = body;
    while (*p) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(end - p));
        if (eq && (size_t)(eq - p) == name_len && !memcmp(p, name, name_len)) {
            if (found) return false;
            found = true;
            size_t used = 0;
            for (const char *v = eq + 1; v < end; ++v) {
                unsigned char c = (unsigned char)*v;
                if (c == '+') c = ' ';
                else if (c == '%') {
                    if (end - v < 3 || hex(v[1]) < 0 || hex(v[2]) < 0) return false;
                    c = (unsigned char)((hex(v[1]) << 4) | hex(v[2]));
                    v += 2;
                }
                if (!c || c < 32 || c == 127 || used + 1 >= capacity) return false;
                out[used++] = (char)c;
            }
            out[used] = 0;
        }
        p = *end ? end + 1 : end;
    }
    return true;
}

bool form_encode(const char *input, char *out, size_t capacity)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        bool plain = isalnum(*p) || *p == '-' || *p == '_' || *p == '.';
        size_t need = plain ? 1 : 3;
        if (n + need >= capacity) return false;
        if (plain) out[n++] = (char)*p;
        else { out[n++] = '%'; out[n++] = digits[*p >> 4]; out[n++] = digits[*p & 15]; }
    }
    if (n >= capacity) return false;
    out[n] = 0;
    return true;
}

bool form_minute(const char *value, uint32_t *minute)
{
    if (strlen(value) != 5 || value[2] != ':' ||
        !isdigit((unsigned char)value[0]) || !isdigit((unsigned char)value[1]) ||
        !isdigit((unsigned char)value[3]) || !isdigit((unsigned char)value[4])) return false;
    unsigned h = (value[0] - '0') * 10 + value[1] - '0';
    unsigned m = (value[3] - '0') * 10 + value[4] - '0';
    if (h > 23 || m > 59) return false;
    *minute = h * 60 + m;
    return true;
}
