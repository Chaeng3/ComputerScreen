#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
// Missing fields are empty; malformed percent escapes and truncation are errors.
bool form_get(const char *body, const char *name, char *out, size_t capacity);
bool form_encode(const char *input, char *out, size_t capacity);
bool form_minute(const char *value, uint32_t *minute);
