#include "handle.h"

static uint32_t pack_u16_u16(uint16_t a, uint16_t b)
{
    return ((uint32_t)a << 16) | (uint32_t)b;
}

static uint16_t hi_u16(uint32_t v)
{
    return (uint16_t)(v >> 16);
}

static uint16_t lo_u16(uint32_t v)
{
    return (uint16_t)(v & 0xFFFFu);
}

ihandle_t ihandle_make(ihandle_type_t type, uint16_t index, uint16_t generation)
{
    ihandle_t h;
    h.value = pack_u16_u16(generation, index);
    h.type = type;
    h.meta = 0;
    return h;
}

ihandle_t ihandle_invalid(void)
{
    ihandle_t h;
    h.value = 0;
    h.type = 0;
    h.meta = 0;
    return h;
}

bool ihandle_is_valid(ihandle_t h)
{
    return h.type != 0 && h.value != 0;
}

bool ihandle_eq(ihandle_t a, ihandle_t b)
{
    return a.value == b.value && a.type == b.type && a.meta == b.meta;
}

uint16_t ihandle_index(ihandle_t h)
{
    return lo_u16(h.value);
}

uint16_t ihandle_generation(ihandle_t h)
{
    return hi_u16(h.value);
}

ihandle_type_t ihandle_type(ihandle_t h)
{
    return h.type;
}

ihandle_t ihandle_with_type(ihandle_t h, ihandle_type_t type)
{
    h.type = type;
    return h;
}

ihandle_t ihandle_with_meta(ihandle_t h, uint16_t meta)
{
    h.meta = meta;
    return h;
}

uint32_t ihandle_hash(ihandle_t h)
{
    uint32_t x = h.value;
    x ^= (uint32_t)h.type * 0x9e3779b9u;
    x ^= (uint32_t)h.meta * 0x85ebca6bu;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
