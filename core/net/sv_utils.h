#pragma once
#include <stdint.h>

typedef union host_u
{
    struct
    {
        uint8_t a, b, c, d;
    };
    uint8_t ip[4];
    uint32_t ip_u32;
} host_t;

void host_to_char(char *out, size_t out_len, host_t host);
host_t char_to_host(const char *in, size_t len);
