#pragma once
#include <stdlib.h>
#include <string.h>

static int loader_str_endswith_ci(const char *s, const char *ext)
{
    if (!s || !ext) return 0;
    size_t ns = strlen(s);
    size_t ne = strlen(ext);
    if (ne > ns) return 0;
    const char *a = s + (ns - ne);
    for (size_t i = 0; i < ne; ++i)
    {
        char c1 = a[i];
        char c2 = ext[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 - 'A' + 'a');
        if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 - 'A' + 'a');
        if (c1 != c2) return 0;
    }
    return 1;
}

static char *loader_path_dirname_dup(const char *path)
{
    if (!path) return NULL;
    size_t n = strlen(path);
    size_t cut = 0;
    for (size_t i = 0; i < n; ++i)
    {
        char c = path[i];
        if (c == '/' || c == '\\') cut = i + 1;
    }
    char *out = (char *)malloc(cut + 1);
    if (!out) return NULL;
    memcpy(out, path, cut);
    out[cut] = 0;
    return out;
}

static char *loader_path_join_dup(const char *a, const char *b)
{
    if (!a || !b) return NULL;
    size_t na = strlen(a);
    size_t nb = strlen(b);
    char need = 0;
    if (na > 0)
    {
        char c = a[na - 1];
        if (c != '/' && c != '\\') need = 1;
    }
    char *out = (char *)malloc(na + (size_t)need + nb + 1);
    if (!out) return NULL;
    memcpy(out, a, na);
    if (need) out[na] = '/';
    memcpy(out + na + (size_t)need, b, nb);
    out[na + (size_t)need + nb] = 0;
    return out;
}

static char *loader_strdup_trim(const char *s)
{
    if (!s) return NULL;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    size_t n = strlen(s);
    while (n > 0)
    {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') n--;
        else break;
    }
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}
