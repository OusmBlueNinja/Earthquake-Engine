#include "iKv1.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "utils/macros.h"

static char *ikv_strdup(const char *s)
{
    if (!s)
        return NULL;
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (!p)
        return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    while (s && *s)
    {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

static ikv_node_t *alloc_node(const char *key)
{
    ikv_node_t *n = (ikv_node_t *)calloc(1, sizeof(*n));
    if (!n)
        return NULL;
    if (key)
    {
        n->key = ikv_strdup(key);
        if (!n->key)
        {
            free(n);
            return NULL;
        }
    }
    return n;
}

static void object_init(ikv_object_t *o, uint32_t buckets)
{
    o->bucket_count = buckets ? buckets : 64;
    o->size = 0;
    o->buckets = (ikv_node_t **)calloc(o->bucket_count, sizeof(ikv_node_t *));
}

static void array_init(ikv_array_t *a, ikv_type_t element_type)
{
    a->element_type = element_type;
    a->count = 0;
    a->items = NULL;
}

static void node_free_payload(ikv_node_t *n)
{
    if (!n)
        return;
    if (n->type == IKV_STRING)
    {
        free(n->value.string);
    }
    else if (n->type == IKV_OBJECT)
    {
        for (uint32_t i = 0; i < n->value.object.bucket_count; i++)
        {
            ikv_node_t *c = n->value.object.buckets[i];
            while (c)
            {
                ikv_node_t *next = c->next;
                ikv_free(c);
                c = next;
            }
        }
        free(n->value.object.buckets);
    }
    else if (n->type == IKV_ARRAY)
    {
        for (uint32_t i = 0; i < n->value.array.count; i++)
            ikv_free(n->value.array.items[i]);
        free(n->value.array.items);
    }
}

void ikv_free(ikv_node_t *n)
{
    if (!n)
        return;
    free(n->key);
    node_free_payload(n);
    free(n);
}

ikv_node_t *ikv_create_object(const char *key)
{
    ikv_node_t *n = alloc_node(key);
    if (!n)
        return NULL;
    n->type = IKV_OBJECT;
    object_init(&n->value.object, 64);
    if (!n->value.object.buckets)
    {
        ikv_free(n);
        return NULL;
    }
    return n;
}

ikv_node_t *ikv_create_array(const char *key, ikv_type_t element_type)
{
    ikv_node_t *n = alloc_node(key);
    if (!n)
        return NULL;
    n->type = IKV_ARRAY;
    array_init(&n->value.array, element_type);
    return n;
}

static ikv_node_t *object_find_node(const ikv_object_t *o, const char *key, ikv_node_t **out_prev, uint32_t *out_bucket)
{
    if (out_prev)
        *out_prev = NULL;
    if (out_bucket)
        *out_bucket = 0;
    if (!o || !o->buckets || !key)
        return NULL;

    uint32_t b = fnv1a(key) % o->bucket_count;
    if (out_bucket)
        *out_bucket = b;

    ikv_node_t *prev = NULL;
    ikv_node_t *cur = o->buckets[b];
    while (cur)
    {
        if (cur->key && strcmp(cur->key, key) == 0)
        {
            if (out_prev)
                *out_prev = prev;
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }
    if (out_prev)
        *out_prev = prev;
    return NULL;
}

static void object_rehash(ikv_object_t *o, uint32_t new_bucket_count)
{
    ikv_node_t **new_buckets = (ikv_node_t **)calloc(new_bucket_count, sizeof(ikv_node_t *));
    if (!new_buckets)
        return;

    for (uint32_t i = 0; i < o->bucket_count; i++)
    {
        ikv_node_t *c = o->buckets[i];
        while (c)
        {
            ikv_node_t *next = c->next;
            uint32_t b = fnv1a(c->key) % new_bucket_count;
            c->next = new_buckets[b];
            new_buckets[b] = c;
            c = next;
        }
    }

    free(o->buckets);
    o->buckets = new_buckets;
    o->bucket_count = new_bucket_count;
}

static void object_maybe_grow(ikv_object_t *o)
{
    if (!o || o->bucket_count == 0)
        return;
    if (o->size * 4 >= o->bucket_count * 3)
        object_rehash(o, o->bucket_count * 2);
}

static void object_put_node(ikv_object_t *o, ikv_node_t *n)
{
    if (!o || !n || !n->key)
        return;

    object_maybe_grow(o);

    uint32_t b = fnv1a(n->key) % o->bucket_count;
    n->next = o->buckets[b];
    o->buckets[b] = n;
    o->size++;
}

static void object_set_node(ikv_object_t *o, ikv_node_t *n)
{
    if (!o || !n || !n->key)
    {
        ikv_free(n);
        return;
    }

    uint32_t bucket = 0;
    ikv_node_t *prev = NULL;
    ikv_node_t *found = object_find_node(o, n->key, &prev, &bucket);

    if (found)
    {
        if (prev)
            prev->next = found->next;
        else
            o->buckets[bucket] = found->next;
        o->size--;
        ikv_free(found);
    }

    object_put_node(o, n);
}

void ikv_object_set_int(ikv_node_t *obj, const char *key, int64_t v)
{
    if (!obj || obj->type != IKV_OBJECT || !key)
        return;
    ikv_node_t *n = alloc_node(key);
    if (!n)
        return;
    n->type = IKV_INT;
    n->value.i = v;
    object_set_node(&obj->value.object, n);
}

void ikv_object_set_bool(ikv_node_t *obj, const char *key, bool v)
{
    if (!obj || obj->type != IKV_OBJECT || !key)
        return;
    ikv_node_t *n = alloc_node(key);
    if (!n)
        return;
    n->type = IKV_BOOL;
    n->value.b = v;
    object_set_node(&obj->value.object, n);
}

void ikv_object_set_float(ikv_node_t *obj, const char *key, double v)
{
    if (!obj || obj->type != IKV_OBJECT || !key)
        return;
    ikv_node_t *n = alloc_node(key);
    if (!n)
        return;
    n->type = IKV_FLOAT;
    n->value.f = v;
    object_set_node(&obj->value.object, n);
}

void ikv_object_set_string(ikv_node_t *obj, const char *key, const char *v)
{
    if (!obj || obj->type != IKV_OBJECT || !key)
        return;
    ikv_node_t *n = alloc_node(key);
    if (!n)
        return;
    n->type = IKV_STRING;
    n->value.string = ikv_strdup(v ? v : "");
    if (!n->value.string)
    {
        ikv_free(n);
        return;
    }
    object_set_node(&obj->value.object, n);
}

ikv_node_t *ikv_object_add_object(ikv_node_t *obj, const char *key)
{
    if (!obj || obj->type != IKV_OBJECT || !key)
        return NULL;
    ikv_node_t *n = ikv_create_object(key);
    if (!n)
        return NULL;
    object_set_node(&obj->value.object, n);
    return n;
}

ikv_node_t *ikv_object_add_array(ikv_node_t *obj, const char *key, ikv_type_t t)
{
    if (!obj || obj->type != IKV_OBJECT || !key)
        return NULL;
    ikv_node_t *n = ikv_create_array(key, t);
    if (!n)
        return NULL;
    object_set_node(&obj->value.object, n);
    return n;
}

static void array_push_node(ikv_array_t *a, ikv_node_t *n)
{
    ikv_node_t **p = (ikv_node_t **)realloc(a->items, sizeof(ikv_node_t *) * (a->count + 1));
    if (!p)
    {
        ikv_free(n);
        return;
    }
    a->items = p;
    a->items[a->count++] = n;
}

static bool array_type_ok(ikv_array_t *a, ikv_type_t t)
{
    if (!a)
        return false;
    if (a->element_type == IKV_NULL)
        return true;
    return a->element_type == t;
}

void ikv_array_add_int(ikv_node_t *arr, int64_t v)
{
    if (!arr || arr->type != IKV_ARRAY)
        return;
    if (!array_type_ok(&arr->value.array, IKV_INT))
        return;
    ikv_node_t *n = alloc_node(NULL);
    if (!n)
        return;
    n->type = IKV_INT;
    n->value.i = v;
    array_push_node(&arr->value.array, n);
}

void ikv_array_add_bool(ikv_node_t *arr, bool v)
{
    if (!arr || arr->type != IKV_ARRAY)
        return;
    if (!array_type_ok(&arr->value.array, IKV_BOOL))
        return;
    ikv_node_t *n = alloc_node(NULL);
    if (!n)
        return;
    n->type = IKV_BOOL;
    n->value.b = v;
    array_push_node(&arr->value.array, n);
}

void ikv_array_add_float(ikv_node_t *arr, double v)
{
    if (!arr || arr->type != IKV_ARRAY)
        return;
    if (!array_type_ok(&arr->value.array, IKV_FLOAT))
        return;
    ikv_node_t *n = alloc_node(NULL);
    if (!n)
        return;
    n->type = IKV_FLOAT;
    n->value.f = v;
    array_push_node(&arr->value.array, n);
}

void ikv_array_add_string(ikv_node_t *arr, const char *v)
{
    if (!arr || arr->type != IKV_ARRAY)
        return;
    if (!array_type_ok(&arr->value.array, IKV_STRING))
        return;
    ikv_node_t *n = alloc_node(NULL);
    if (!n)
        return;
    n->type = IKV_STRING;
    n->value.string = ikv_strdup(v ? v : "");
    if (!n->value.string)
    {
        ikv_free(n);
        return;
    }
    array_push_node(&arr->value.array, n);
}

ikv_node_t *ikv_array_add_object(ikv_node_t *arr)
{
    if (!arr || arr->type != IKV_ARRAY)
        return NULL;
    if (!array_type_ok(&arr->value.array, IKV_OBJECT))
        return NULL;
    ikv_node_t *n = ikv_create_object(NULL);
    if (!n)
        return NULL;
    array_push_node(&arr->value.array, n);
    return n;
}

ikv_node_t *ikv_object_get(const ikv_node_t *obj, const char *key)
{
    if (!obj || obj->type != IKV_OBJECT || !key)
        return NULL;
    uint32_t b = fnv1a(key) % obj->value.object.bucket_count;
    ikv_node_t *n = obj->value.object.buckets[b];
    while (n)
    {
        if (n->key && strcmp(n->key, key) == 0)
            return n;
        n = n->next;
    }
    return NULL;
}

ikv_node_t *ikv_array_get(const ikv_node_t *arr, uint32_t index)
{
    if (!arr || arr->type != IKV_ARRAY)
        return NULL;
    if (index >= arr->value.array.count)
        return NULL;
    return arr->value.array.items[index];
}

const char *ikv_as_string(const ikv_node_t *n) { return (n && n->type == IKV_STRING && n->value.string) ? n->value.string : ""; }
int64_t ikv_as_int(const ikv_node_t *n) { return (n && n->type == IKV_INT) ? n->value.i : 0; }
double ikv_as_float(const ikv_node_t *n) { return (n && n->type == IKV_FLOAT) ? n->value.f : 0.0; }
bool ikv_as_bool(const ikv_node_t *n) { return (n && n->type == IKV_BOOL) ? n->value.b : false; }

static void write_indent(FILE *f, int indent)
{
    for (int i = 0; i < indent; i++)
        fputc(' ', f);
}

static void write_escaped_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (const char *p = s ? s : ""; *p; p++)
    {
        unsigned char c = (unsigned char)*p;
        if (c == '\\')
        {
            fputs("\\\\", f);
        }
        else if (c == '"')
        {
            fputs("\\\"", f);
        }
        else if (c == '\n')
        {
            fputs("\\n", f);
        }
        else if (c == '\r')
        {
            fputs("\\r", f);
        }
        else if (c == '\t')
        {
            fputs("\\t", f);
        }
        else
        {
            fputc(c, f);
        }
    }
    fputc('"', f);
}

static void write_node(FILE *f, const ikv_node_t *n, int indent);

static void write_value(FILE *f, const ikv_node_t *n, int indent)
{
    switch (n->type)
    {
    case IKV_NULL:
        fputs("null", f);
        break;
    case IKV_STRING:
        write_escaped_string(f, n->value.string ? n->value.string : "");
        break;
    case IKV_INT:
        fprintf(f, "%lld", (long long)n->value.i);
        break;
    case IKV_BOOL:
        fputs(n->value.b ? "true" : "false", f);
        break;
    case IKV_FLOAT:
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", n->value.f);
        fputs(buf, f);
    }
    break;
    case IKV_OBJECT:
    {
        fputs("{\n", f);
        for (uint32_t i = 0; i < n->value.object.bucket_count; i++)
        {
            for (ikv_node_t *c = n->value.object.buckets[i]; c; c = c->next)
                write_node(f, c, indent + 4);
        }
        write_indent(f, indent);
        fputc('}', f);
    }
    break;
    case IKV_ARRAY:
    {
        fputs("[\n", f);
        for (uint32_t i = 0; i < n->value.array.count; i++)
        {
            write_indent(f, indent + 4);
            write_value(f, n->value.array.items[i], indent + 4);
            fputc('\n', f);
        }
        write_indent(f, indent);
        fputc(']', f);
    }
    break;
    default:
        break;
    }
}

static void write_node(FILE *f, const ikv_node_t *n, int indent)
{
    write_indent(f, indent);
    if (n->key)
    {
        write_escaped_string(f, n->key);
        fputc(' ', f);
    }
    write_value(f, n, indent);
    fputc('\n', f);
}

bool ikv_write_file(const char *path, const ikv_node_t *root)
{
    if (!path || !root)
        return false;

    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    if (root->type != IKV_OBJECT)
    {
        write_value(f, root, 0);
        fputc('\n', f);
        fclose(f);
        return true;
    }

    fputs("ikv" STR(iKv_VERSION) " ", f);
    write_escaped_string(f, (root->key && root->key[0]) ? root->key : "root");
    fputc('\n', f);

    fputs("{\n", f);
    for (uint32_t i = 0; i < root->value.object.bucket_count; i++)
    {
        for (ikv_node_t *c = root->value.object.buckets[i]; c; c = c->next)
            write_node(f, c, 4);
    }
    fputs("}\n", f);

    fclose(f);
    return true;
}

typedef enum
{
    T_EOF,
    T_LBRACE,
    T_RBRACE,
    T_LBRACK,
    T_RBRACK,
    T_COMMA,
    T_STRING,
    T_WORD
} tok_type;

typedef struct
{
    tok_type type;
    const char *start;
    size_t len;
} token;

typedef struct
{
    const char *p;
} lexer;

static const char *lex_skip_ws(const char *p)
{
    while (*p)
    {
        if (isspace((unsigned char)*p))
        {
            p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '/')
        {
            p += 2;
            while (*p && *p != '\n')
                p++;
            continue;
        }
        if (p[0] == '#')
        {
            p += 1;
            while (*p && *p != '\n')
                p++;
            continue;
        }
        break;
    }
    return p;
}

static token lex_next(lexer *l)
{
    token t;
    t.type = T_EOF;
    t.start = l->p;
    t.len = 0;

    l->p = lex_skip_ws(l->p);
    const char *p = l->p;
    if (!*p)
    {
        t.type = T_EOF;
        return t;
    }

    if (*p == '{')
    {
        l->p = p + 1;
        t.type = T_LBRACE;
        return t;
    }
    if (*p == '}')
    {
        l->p = p + 1;
        t.type = T_RBRACE;
        return t;
    }
    if (*p == '[')
    {
        l->p = p + 1;
        t.type = T_LBRACK;
        return t;
    }
    if (*p == ']')
    {
        l->p = p + 1;
        t.type = T_RBRACK;
        return t;
    }
    if (*p == ',')
    {
        l->p = p + 1;
        t.type = T_COMMA;
        return t;
    }

    if (*p == '"')
    {
        p++;
        const char *s = p;
        while (*p)
        {
            if (*p == '\\' && p[1])
            {
                p += 2;
                continue;
            }
            if (*p == '"')
                break;
            p++;
        }
        if (*p != '"')
        {
            l->p = p;
            t.type = T_EOF;
            return t;
        }
        t.type = T_STRING;
        t.start = s;
        t.len = (size_t)(p - s);
        l->p = p + 1;
        return t;
    }

    const char *s = p;
    while (*p && !isspace((unsigned char)*p) && *p != '{' && *p != '}' && *p != '[' && *p != ']' && *p != ',')
        p++;
    t.type = T_WORD;
    t.start = s;
    t.len = (size_t)(p - s);
    l->p = p;
    return t;
}

static char *unescape_string(const char *s, size_t len)
{
    char *out = (char *)malloc(len + 1);
    if (!out)
        return NULL;
    size_t w = 0;
    for (size_t i = 0; i < len; i++)
    {
        char c = s[i];
        if (c == '\\' && i + 1 < len)
        {
            char n = s[i + 1];
            if (n == 'n')
            {
                out[w++] = '\n';
                i++;
                continue;
            }
            if (n == 'r')
            {
                out[w++] = '\r';
                i++;
                continue;
            }
            if (n == 't')
            {
                out[w++] = '\t';
                i++;
                continue;
            }
            if (n == '\\')
            {
                out[w++] = '\\';
                i++;
                continue;
            }
            if (n == '"')
            {
                out[w++] = '"';
                i++;
                continue;
            }
        }
        out[w++] = c;
    }
    out[w] = 0;
    return out;
}

static char *token_to_cstr(const token *t)
{
    char *s = (char *)malloc(t->len + 1);
    if (!s)
        return NULL;
    memcpy(s, t->start, t->len);
    s[t->len] = 0;
    return s;
}

static ikv_node_t *parse_value_lex(lexer *l);

static ikv_node_t *parse_array_lex(lexer *l)
{
    ikv_node_t *arr = ikv_create_array(NULL, IKV_NULL);
    if (!arr)
        return NULL;

    for (;;)
    {
        const char *save = l->p;
        token t = lex_next(l);
        if (t.type == T_RBRACK)
            break;
        if (t.type == T_EOF)
        {
            ikv_free(arr);
            return NULL;
        }
        if (t.type == T_COMMA)
            continue;
        l->p = save;

        ikv_node_t *v = parse_value_lex(l);
        if (!v)
        {
            ikv_free(arr);
            return NULL;
        }

        if (arr->value.array.element_type == IKV_NULL && v->type != IKV_NULL)
            arr->value.array.element_type = v->type;

        if (arr->value.array.element_type != IKV_NULL && v->type != IKV_NULL && v->type != arr->value.array.element_type)
            arr->value.array.element_type = IKV_NULL;

        array_push_node(&arr->value.array, v);

        save = l->p;
        t = lex_next(l);
        if (t.type == T_RBRACK)
            break;
        if (t.type == T_EOF)
        {
            ikv_free(arr);
            return NULL;
        }
        if (t.type != T_COMMA)
            l->p = save;
    }

    return arr;
}

static ikv_node_t *parse_object_lex(lexer *l)
{
    ikv_node_t *obj = ikv_create_object(NULL);
    if (!obj)
        return NULL;

    for (;;)
    {
        token k = lex_next(l);
        if (k.type == T_RBRACE)
            break;
        if (k.type == T_EOF)
        {
            ikv_free(obj);
            return NULL;
        }
        if (k.type == T_COMMA)
            continue;
        if (k.type != T_STRING)
        {
            ikv_free(obj);
            return NULL;
        }

        char *key = unescape_string(k.start, k.len);
        if (!key)
        {
            ikv_free(obj);
            return NULL;
        }

        ikv_node_t *val = parse_value_lex(l);
        if (!val)
        {
            free(key);
            ikv_free(obj);
            return NULL;
        }

        free(val->key);
        val->key = key;
        object_set_node(&obj->value.object, val);

        const char *save = l->p;
        token t = lex_next(l);
        if (t.type == T_RBRACE)
            break;
        if (t.type == T_EOF)
        {
            ikv_free(obj);
            return NULL;
        }
        if (t.type != T_COMMA)
            l->p = save;
    }

    return obj;
}

static ikv_node_t *parse_value_lex(lexer *l)
{
    token t = lex_next(l);

    if (t.type == T_LBRACE)
        return parse_object_lex(l);
    if (t.type == T_LBRACK)
        return parse_array_lex(l);

    if (t.type == T_STRING)
    {
        ikv_node_t *n = alloc_node(NULL);
        if (!n)
            return NULL;
        n->type = IKV_STRING;
        n->value.string = unescape_string(t.start, t.len);
        if (!n->value.string)
        {
            ikv_free(n);
            return NULL;
        }
        return n;
    }

    if (t.type == T_WORD)
    {
        char *w = token_to_cstr(&t);
        if (!w)
            return NULL;

        ikv_node_t *n = alloc_node(NULL);
        if (!n)
        {
            free(w);
            return NULL;
        }

        if (strcmp(w, "true") == 0)
        {
            n->type = IKV_BOOL;
            n->value.b = true;
            free(w);
            return n;
        }
        if (strcmp(w, "false") == 0)
        {
            n->type = IKV_BOOL;
            n->value.b = false;
            free(w);
            return n;
        }
        if (strcmp(w, "null") == 0)
        {
            n->type = IKV_NULL;
            free(w);
            return n;
        }

        char *endp = NULL;
        double df = strtod(w, &endp);
        if (endp && *endp == 0)
        {
            if (strchr(w, '.') || strchr(w, 'e') || strchr(w, 'E'))
            {
                n->type = IKV_FLOAT;
                n->value.f = df;
                free(w);
                return n;
            }
            else
            {
                long long di = strtoll(w, &endp, 10);
                if (endp && *endp == 0)
                {
                    n->type = IKV_INT;
                    n->value.i = (int64_t)di;
                    free(w);
                    return n;
                }
            }
        }

        n->type = IKV_STRING;
        n->value.string = w;
        return n;
    }

    return NULL;
}

ikv_node_t *ikv_parse_string(const char *src)
{
    if (!src)
        return NULL;

    lexer l;
    l.p = src;

    const char *save0 = l.p;
    token t0 = lex_next(&l);

    if (t0.type == T_WORD)
    {
        char *w0 = token_to_cstr(&t0);
        if (!w0)
            return NULL;

        if (strcmp(w0, "ikv1") == 0)
        {
            free(w0);

            token tn = lex_next(&l);
            if (tn.type != T_STRING && tn.type != T_WORD)
                return NULL;

            char *name = (tn.type == T_STRING) ? unescape_string(tn.start, tn.len) : token_to_cstr(&tn);
            if (!name)
                return NULL;

            token tb = lex_next(&l);
            if (tb.type != T_LBRACE)
            {
                free(name);
                return NULL;
            }

            ikv_node_t *obj = parse_object_lex(&l);
            if (!obj)
            {
                free(name);
                return NULL;
            }

            free(obj->key);
            obj->key = name;
            return obj;
        }

        free(w0);
        l.p = save0;
    }
    else if (t0.type == T_LBRACE)
    {
        return parse_object_lex(&l);
    }
    else
    {
        l.p = save0;
    }

    ikv_node_t *root = ikv_create_object(NULL);
    if (!root)
        return NULL;

    for (;;)
    {
        const char *s2 = l.p;
        token k = lex_next(&l);
        if (k.type == T_EOF)
            break;
        if (k.type == T_COMMA)
            continue;
        if (k.type != T_STRING)
        {
            ikv_free(root);
            return NULL;
        }

        char *key = unescape_string(k.start, k.len);
        if (!key)
        {
            ikv_free(root);
            return NULL;
        }

        ikv_node_t *val = parse_value_lex(&l);
        if (!val)
        {
            free(key);
            ikv_free(root);
            return NULL;
        }

        free(val->key);
        val->key = key;
        object_set_node(&root->value.object, val);

        s2 = l.p;
        token sep = lex_next(&l);
        if (sep.type == T_EOF)
            break;
        if (sep.type != T_COMMA)
            l.p = s2;
    }

    return root;
}

ikv_node_t *ikv_parse_file(const char *path)
{
    if (!path)
        return NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0)
    {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }

    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = 0;

    ikv_node_t *root = ikv_parse_string(buf);
    free(buf);
    return root;
}
