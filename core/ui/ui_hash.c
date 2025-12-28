#include "ui_hash.h"
#include <stdint.h>

uint32_t ui_hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    if (!s) return h;
    while (*s)
    {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

uint32_t ui_hash_ptr(const void *p)
{
    uintptr_t v = (uintptr_t)p;
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < (uint32_t)sizeof(uintptr_t); ++i)
    {
        h ^= (uint8_t)(v & 0xffu);
        h *= 16777619u;
        v >>= 8u;
    }
    return h;
}

uint32_t ui_hash_combine(uint32_t a, uint32_t b)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < 4; ++i) { h ^= (uint8_t)(a & 0xffu); h *= 16777619u; a >>= 8u; }
    for (uint32_t i = 0; i < 4; ++i) { h ^= (uint8_t)(b & 0xffu); h *= 16777619u; b >>= 8u; }
    return h;
}
