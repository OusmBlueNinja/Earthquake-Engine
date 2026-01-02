#include "miniyaml.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

static void myaml_rtrim(char *s)
{
    if (!s)
        return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
}

static const char *myaml_lskip_ws(const char *s)
{
    if (!s)
        return "";
    while (*s == ' ' || *s == '\t')
        ++s;
    return s;
}

static void myaml_w_indent(FILE *f, int spaces)
{
    for (int i = 0; i < spaces; ++i)
        fputc(' ', f);
}

void myaml_writer_init(myaml_writer_t *w, FILE *f)
{
    if (!w)
        return;
    w->f = f;
}

void myaml_write_indent(myaml_writer_t *w, int spaces)
{
    if (!w || !w->f)
        return;
    myaml_w_indent(w->f, spaces);
}

void myaml_write_raw(myaml_writer_t *w, const char *s)
{
    if (!w || !w->f || !s)
        return;
    fputs(s, w->f);
}

void myaml_write_linef(myaml_writer_t *w, int indent_spaces, const char *fmt, ...)
{
    if (!w || !w->f || !fmt)
        return;
    myaml_w_indent(w->f, indent_spaces);
    va_list args;
    va_start(args, fmt);
    vfprintf(w->f, fmt, args);
    va_end(args);
    fputc('\n', w->f);
}

void myaml_write_quoted(myaml_writer_t *w, const char *s)
{
    if (!w || !w->f)
        return;

    if (!s)
        s = "";

    fputc('"', w->f);
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
        unsigned char c = *p;
        switch (c)
        {
        case '\\':
            fputs("\\\\", w->f);
            break;
        case '"':
            fputs("\\\"", w->f);
            break;
        case '\n':
            fputs("\\n", w->f);
            break;
        case '\r':
            fputs("\\r", w->f);
            break;
        case '\t':
            fputs("\\t", w->f);
            break;
        default:
            if (c < 0x20)
            {
                // control characters -> escape as \xNN
                fprintf(w->f, "\\x%02X", (unsigned)c);
            }
            else
            {
                fputc((int)c, w->f);
            }
            break;
        }
    }
    fputc('"', w->f);
}

void myaml_write_key(myaml_writer_t *w, int indent_spaces, const char *key)
{
    if (!w || !w->f || !key)
        return;
    myaml_w_indent(w->f, indent_spaces);
    fputs(key, w->f);
    fputs(":\n", w->f);
}

void myaml_write_key_str(myaml_writer_t *w, int indent_spaces, const char *key, const char *value)
{
    if (!w || !w->f || !key)
        return;
    myaml_w_indent(w->f, indent_spaces);
    fputs(key, w->f);
    fputs(": ", w->f);
    myaml_write_quoted(w, value);
    fputc('\n', w->f);
}

void myaml_write_key_u32(myaml_writer_t *w, int indent_spaces, const char *key, uint32_t value)
{
    myaml_write_linef(w, indent_spaces, "%s: %u", key ? key : "", (unsigned)value);
}

void myaml_write_key_i32(myaml_writer_t *w, int indent_spaces, const char *key, int32_t value)
{
    myaml_write_linef(w, indent_spaces, "%s: %d", key ? key : "", (int)value);
}

void myaml_write_key_f32(myaml_writer_t *w, int indent_spaces, const char *key, float value)
{
    myaml_write_linef(w, indent_spaces, "%s: %.9g", key ? key : "", (double)value);
}

void myaml_write_key_bool(myaml_writer_t *w, int indent_spaces, const char *key, int value01)
{
    myaml_write_linef(w, indent_spaces, "%s: %d", key ? key : "", value01 ? 1 : 0);
}

int myaml_reader_load_file(myaml_reader_t *r, const char *path)
{
    if (!r)
        return 0;
    *r = (myaml_reader_t){0};
    if (!path || !path[0])
        return 0;

    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0)
    {
        fclose(f);
        return 0;
    }

    r->data = (char *)malloc((size_t)sz + 1);
    if (!r->data)
    {
        fclose(f);
        return 0;
    }
    size_t got = fread(r->data, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz)
    {
        free(r->data);
        *r = (myaml_reader_t){0};
        return 0;
    }

    r->data[sz] = 0;
    r->size = (uint32_t)sz;
    r->off = 0;
    r->line_no = 0;
    return 1;
}

void myaml_reader_free(myaml_reader_t *r)
{
    if (!r)
        return;
    free(r->data);
    *r = (myaml_reader_t){0};
}

int myaml_next_line(myaml_reader_t *r, const char **out_line, int *out_indent, int *out_is_seq)
{
    if (out_line)
        *out_line = NULL;
    if (out_indent)
        *out_indent = 0;
    if (out_is_seq)
        *out_is_seq = 0;

    if (!r || !r->data || r->off >= r->size)
        return 0;

    for (;;)
    {
        if (r->off >= r->size)
            return 0;

        char *line = r->data + r->off;
        char *nl = line;
        while (*nl && *nl != '\n')
            ++nl;
        if (*nl == '\n')
            *nl++ = 0;

        r->off = (uint32_t)(nl - r->data);
        r->line_no++;

        myaml_rtrim(line);

        // Strip comments (outside quotes; we only support simple quoted strings on one line).
        int in_q = 0;
        for (char *p = line; *p; ++p)
        {
            if (*p == '"' && (p == line || p[-1] != '\\'))
                in_q = !in_q;
            if (!in_q && *p == '#')
            {
                *p = 0;
                myaml_rtrim(line);
                break;
            }
        }

        const char *t = myaml_lskip_ws(line);
        if (!t[0])
            continue; // blank

        int indent = 0;
        for (const char *p = line; *p == ' '; ++p)
            indent++;

        int is_seq = 0;
        const char *content = line + indent;
        if (content[0] == '-' && content[1] == ' ')
        {
            is_seq = 1;
            content += 2;
        }
        content = myaml_lskip_ws(content);

        if (out_line)
            *out_line = content;
        if (out_indent)
            *out_indent = indent;
        if (out_is_seq)
            *out_is_seq = is_seq;
        return 1;
    }
}

int myaml_split_kv(const char *line, const char **out_key, const char **out_value)
{
    if (out_key)
        *out_key = "";
    if (out_value)
        *out_value = "";
    if (!line)
        return 0;

    const char *p = line;
    const char *colon = NULL;
    int in_q = 0;
    while (*p)
    {
        if (*p == '"' && (p == line || p[-1] != '\\'))
            in_q = !in_q;
        if (!in_q && *p == ':')
        {
            colon = p;
            break;
        }
        ++p;
    }
    if (!colon)
        return 0;

    // key: [value]
    const char *k0 = line;
    const char *k1 = colon;
    while (k1 > k0 && (k1[-1] == ' ' || k1[-1] == '\t'))
        --k1;

    const char *v0 = colon + 1;
    v0 = myaml_lskip_ws(v0);

    if (out_key)
        *out_key = k0;
    if (out_value)
        *out_value = v0;

    // NOTE: key is not null-terminated; callers generally compare via strncmp to known literals.
    // This helper is meant for single-line parsing where callers already have the line buffer.
    return 1;
}

static int myaml_parse_u64_base10(const char *s, uint64_t *out)
{
    if (!s || !out)
        return 0;
    s = myaml_lskip_ws(s);
    if (!*s)
        return 0;
    char *endp = NULL;
    unsigned long long v = strtoull(s, &endp, 10);
    if (endp == s)
        return 0;
    endp = (char *)myaml_lskip_ws(endp);
    if (*endp)
        return 0;
    *out = (uint64_t)v;
    return 1;
}

int myaml_parse_u32(const char *s, uint32_t *out)
{
    uint64_t v = 0;
    if (!myaml_parse_u64_base10(s, &v))
        return 0;
    if (v > 0xFFFFFFFFull)
        return 0;
    *out = (uint32_t)v;
    return 1;
}

int myaml_parse_i32(const char *s, int32_t *out)
{
    if (!s || !out)
        return 0;
    s = myaml_lskip_ws(s);
    if (!*s)
        return 0;
    char *endp = NULL;
    long v = strtol(s, &endp, 10);
    if (endp == s)
        return 0;
    endp = (char *)myaml_lskip_ws(endp);
    if (*endp)
        return 0;
    *out = (int32_t)v;
    return 1;
}

int myaml_parse_f32(const char *s, float *out)
{
    if (!s || !out)
        return 0;
    s = myaml_lskip_ws(s);
    if (!*s)
        return 0;
    char *endp = NULL;
    float v = strtof(s, &endp);
    if (endp == s)
        return 0;
    endp = (char *)myaml_lskip_ws(endp);
    if (*endp)
        return 0;
    *out = v;
    return 1;
}

int myaml_parse_bool01(const char *s, int *out01)
{
    uint32_t v = 0;
    if (!out01)
        return 0;
    if (!myaml_parse_u32(s, &v))
        return 0;
    *out01 = (v != 0) ? 1 : 0;
    return 1;
}

int myaml_unquote_inplace(char *s)
{
    if (!s)
        return 0;
    s = (char *)myaml_lskip_ws(s);
    myaml_rtrim(s);
    size_t n = strlen(s);
    if (n < 2)
        return 0;
    if (s[0] != '"' || s[n - 1] != '"')
        return 0;
    s[n - 1] = 0;
    memmove(s, s + 1, n - 1);

    // unescape in-place
    char *w = s;
    for (char *p = s; *p; ++p)
    {
        if (*p == '\\')
        {
            ++p;
            if (!*p)
                break;
            switch (*p)
            {
            case 'n':
                *w++ = '\n';
                break;
            case 'r':
                *w++ = '\r';
                break;
            case 't':
                *w++ = '\t';
                break;
            case '\\':
                *w++ = '\\';
                break;
            case '"':
                *w++ = '"';
                break;
            case 'x':
            {
                // \xNN
                if (isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2]))
                {
                    char hh[3] = {p[1], p[2], 0};
                    unsigned long v = strtoul(hh, NULL, 16);
                    *w++ = (char)(unsigned char)v;
                    p += 2;
                }
                break;
            }
            default:
                *w++ = *p;
                break;
            }
        }
        else
        {
            *w++ = *p;
        }
    }
    *w = 0;
    return 1;
}

