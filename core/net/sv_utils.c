#include "server.h"
#include <ctype.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

void host_to_char(char *out, size_t out_len, host_t host)
{
    if (!out || out_len == 0)
        return;

    int n = snprintf(out, out_len, "%u.%u.%u.%u",
                     (unsigned)host.ip[0],
                     (unsigned)host.ip[1],
                     (unsigned)host.ip[2],
                     (unsigned)host.ip[3]);

    if (n < 0)
        out[0] = 0;
    else if ((size_t)n >= out_len)
        out[out_len - 1] = 0;
}

host_t char_to_host(const char *in, size_t len)
{
    host_t out;
    out.ip[0] = 0;
    out.ip[1] = 0;
    out.ip[2] = 0;
    out.ip[3] = 0;

    if (!in || len == 0)
        return out;

    size_t i = 0;
    while (i < len && isspace((unsigned char)in[i]))
        i++;

    int part = 0;
    unsigned value = 0;
    int digits = 0;

    for (; i < len; ++i)
    {
        unsigned char ch = (unsigned char)in[i];

        if (ch == 0)
            break;

        if (ch >= '0' && ch <= '9')
        {
            value = value * 10u + (unsigned)(ch - '0');
            if (value > 255u)
                return out;
            digits++;
            continue;
        }

        if (ch == '.')
        {
            if (digits == 0)
                return out;
            if (part >= 4)
                return out;
            out.ip[part] = (uint8_t)value;
            part++;
            value = 0;
            digits = 0;
            continue;
        }

        if (isspace(ch))
        {
            while (i < len && in[i] && isspace((unsigned char)in[i]))
                i++;
            break;
        }

        return out;
    }

    if (digits == 0)
        return out;
    if (part != 3)
        return out;
    out.ip[3] = (uint8_t)value;

    return out;
}