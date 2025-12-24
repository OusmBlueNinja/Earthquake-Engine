#pragma once

#include <stdlib.h>
#include <string.h>

static char *dup_cstr(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}