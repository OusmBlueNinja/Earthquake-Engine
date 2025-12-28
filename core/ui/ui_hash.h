#pragma once
#include <stdint.h>

uint32_t ui_hash_str(const char *s);
uint32_t ui_hash_ptr(const void *p);
uint32_t ui_hash_combine(uint32_t a, uint32_t b);
