#ifndef IHANDLE_H
#define IHANDLE_H

#include <stdint.h>
#include <stdbool.h>

typedef uint16_t ihandle_type_t;

typedef struct ihandle_t
{
    uint32_t value;
    ihandle_type_t type;
    uint16_t meta;
} ihandle_t;

ihandle_t ihandle_make(ihandle_type_t type, uint16_t index, uint16_t generation);
ihandle_t ihandle_invalid(void);

bool ihandle_is_valid(ihandle_t h);
bool ihandle_eq(ihandle_t a, ihandle_t b);

uint16_t ihandle_index(ihandle_t h);
uint16_t ihandle_generation(ihandle_t h);

ihandle_type_t ihandle_type(ihandle_t h);

ihandle_t ihandle_with_type(ihandle_t h, ihandle_type_t type);
ihandle_t ihandle_with_meta(ihandle_t h, uint16_t meta);

uint32_t ihandle_hash(ihandle_t h);

#endif
