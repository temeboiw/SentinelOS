#pragma once
#include <stdint.h>

/* Shared, host-testable implementation of the fixed SentinelOS temp path. */
static inline uint32_t K32GetTempPathA(uint32_t capacity, char *out,
                                       uint32_t *last_error) {
    static const char path[] = "C:\\TEMP\\";
    const uint32_t length = (uint32_t)(sizeof(path) - 1);
    if (!out || capacity <= length) {
        if (out && capacity) out[0] = 0;
        if (last_error) *last_error = 122;
        return length + 1;
    }
    for (uint32_t i = 0; i <= length; ++i) out[i] = path[i];
    if (last_error) *last_error = 0;
    return length;
}

static inline uint32_t K32GetTempPathW(uint32_t capacity, uint16_t *out,
                                       uint32_t *last_error) {
    static const uint16_t path[] = {'C', ':', '\\', 'T', 'E', 'M', 'P', '\\', 0};
    const uint32_t length = (uint32_t)(sizeof(path) / sizeof(path[0]) - 1);
    if (!out || capacity <= length) {
        if (out && capacity) out[0] = 0;
        if (last_error) *last_error = 122;
        return length + 1;
    }
    for (uint32_t i = 0; i <= length; ++i) out[i] = path[i];
    if (last_error) *last_error = 0;
    return length;
}
