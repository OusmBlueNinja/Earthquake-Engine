#include "asset_manager/asset_manager.h"
#include "asset_manager/asset_types/material.h"
#include "core.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

typedef struct
{
    char *p;
    uint32_t n;
    uint32_t cap;
} sb_t;

static bool sb_reserve(sb_t *b, uint32_t add)
{
    uint32_t need = b->n + add + 1u;
    if (need <= b->cap)
        return true;
    uint32_t nc = b->cap ? b->cap : 256u;
    while (nc < need)
        nc = nc + (nc >> 1) + 64u;
    char *np = (char *)realloc(b->p, (size_t)nc);
    if (!np)
        return false;
    b->p = np;
    b->cap = nc;
    return true;
}

static bool sb_push_n(sb_t *b, const char *s, uint32_t n)
{
    if (!sb_reserve(b, n))
        return false;
    memcpy(b->p + b->n, s, (size_t)n);
    b->n += n;
    b->p[b->n] = 0;
    return true;
}

static bool sb_push(sb_t *b, const char *s)
{
    if (!s)
        return sb_push_n(b, "", 0);
    return sb_push_n(b, s, (uint32_t)strlen(s));
}

static bool sb_pushf(sb_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0)
    {
        va_end(ap2);
        return false;
    }
    if (!sb_reserve(b, (uint32_t)need))
    {
        va_end(ap2);
        return false;
    }
    vsnprintf(b->p + b->n, (size_t)(b->cap - b->n), fmt, ap2);
    va_end(ap2);
    b->n += (uint32_t)need;
    b->p[b->n] = 0;
    return true;
}

static bool sb_indent(sb_t *b, int indent)
{
    for (int i = 0; i < indent; ++i)
        if (!sb_push(b, "    "))
            return false;
    return true;
}

static bool sb_push_escaped_string(sb_t *b, const char *s)
{
    if (!sb_push(b, "\""))
        return false;

    if (s)
    {
        for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
        {
            unsigned char c = *p;
            if (c == '\\')
            {
                if (!sb_push(b, "\\\\"))
                    return false;
            }
            else if (c == '"')
            {
                if (!sb_push(b, "\\\""))
                    return false;
            }
            else if (c == '\n')
            {
                if (!sb_push(b, "\\n"))
                    return false;
            }
            else if (c == '\r')
            {
                if (!sb_push(b, "\\r"))
                    return false;
            }
            else if (c == '\t')
            {
                if (!sb_push(b, "\\t"))
                    return false;
            }
            else if (c < 32)
            {
                if (!sb_pushf(b, "\\x%02x", (unsigned)c))
                    return false;
            }
            else
            {
                char ch = (char)c;
                if (!sb_push_n(b, &ch, 1))
                    return false;
            }
        }
    }

    if (!sb_push(b, "\""))
        return false;

    return true;
}

static bool ikv_emit_value(sb_t *b, const ikv_node_t *node, int indent);

static bool ikv_emit_object(sb_t *b, const ikv_node_t *node, int indent)
{
    const ikv_object_t *o = &node->value.object;
    if (!o || !o->buckets || !o->bucket_count)
        return true;

    for (uint32_t bi = 0; bi < o->bucket_count; ++bi)
    {
        const ikv_node_t *cur = o->buckets[bi];
        while (cur)
        {
            if (!sb_indent(b, indent))
                return false;

            if (!sb_push_escaped_string(b, cur->key ? cur->key : ""))
                return false;

            if (!sb_push(b, " "))
                return false;

            if (cur->type == IKV_OBJECT)
            {
                if (!sb_push(b, "{\n"))
                    return false;
                if (!ikv_emit_object(b, cur, indent + 1))
                    return false;
                if (!sb_indent(b, indent))
                    return false;
                if (!sb_push(b, "}\n"))
                    return false;
            }
            else if (cur->type == IKV_ARRAY)
            {
                if (!sb_push(b, "[\n"))
                    return false;

                const ikv_array_t *a = &cur->value.array;
                for (uint32_t i = 0; a && i < a->count; ++i)
                {
                    const ikv_node_t *it = a->items ? a->items[i] : NULL;
                    if (!it)
                        continue;

                    if (it->type == IKV_OBJECT)
                    {
                        if (!sb_indent(b, indent + 1))
                            return false;
                        if (!sb_push(b, "{\n"))
                            return false;
                        if (!ikv_emit_object(b, it, indent + 2))
                            return false;
                        if (!sb_indent(b, indent + 1))
                            return false;
                        if (!sb_push(b, "}\n"))
                            return false;
                    }
                    else
                    {
                        if (!sb_indent(b, indent + 1))
                            return false;
                        if (!ikv_emit_value(b, it, indent + 1))
                            return false;
                        if (!sb_push(b, "\n"))
                            return false;
                    }
                }

                if (!sb_indent(b, indent))
                    return false;
                if (!sb_push(b, "]\n"))
                    return false;
            }
            else
            {
                if (!ikv_emit_value(b, cur, indent))
                    return false;
                if (!sb_push(b, "\n"))
                    return false;
            }

            cur = cur->next;
        }
    }

    return true;
}

static bool ikv_emit_value(sb_t *b, const ikv_node_t *node, int indent)
{
    (void)indent;

    switch (node->type)
    {
    case IKV_NULL:
        return sb_push(b, "null");
    case IKV_STRING:
        return sb_push_escaped_string(b, node->value.string ? node->value.string : "");
    case IKV_INT:
        return sb_pushf(b, "%lld", (long long)node->value.i);
    case IKV_FLOAT:
        return sb_pushf(b, "%.17g", node->value.f);
    case IKV_BOOL:
        return sb_push(b, node->value.b ? "true" : "false");
    case IKV_OBJECT:
        return true;
    case IKV_ARRAY:
        return true;
    default:
        break;
    }

    return false;
}

static bool ikv_write_to_memory(const ikv_node_t *root, void **out_bytes, uint32_t *out_bytes_n)
{
    if (!root || !out_bytes || !out_bytes_n)
        return false;

    sb_t sb;
    memset(&sb, 0, sizeof(sb));

    const char *k = root->key ? root->key : "root";

    bool ok = true;
    ok = ok && sb_push(&sb, "ikv1 ");
    ok = ok && sb_push_escaped_string(&sb, k);
    ok = ok && sb_push(&sb, "\n{\n");
    ok = ok && ikv_emit_object(&sb, root, 1);
    ok = ok && sb_push(&sb, "}\n");

    if (!ok)
    {
        free(sb.p);
        return false;
    }

    *out_bytes = sb.p;
    *out_bytes_n = sb.n;
    return true;
}

static bool asset_material_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset)
{
    (void)am;

    if (!out_asset || !path)
        return false;
    if (path_is_ptr)
        return false;

    asset_material_t m = material_make_default(get_renderer()->default_shader_id);
    if (!material_load_file(path, &m))
        return false;

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_MATERIAL;
    out_asset->state = ASSET_STATE_LOADING;
    out_asset->as.material = m;

    return true;
}

static bool asset_material_init(asset_manager_t *am, asset_any_t *asset)
{
    (void)am;

    if (!asset || asset->type != ASSET_MATERIAL)
        return false;

    return true;
}

static void asset_material_cleanup(asset_manager_t *am, asset_any_t *asset)
{
    (void)am;

    if (!asset || asset->type != ASSET_MATERIAL)
        return;

    free(asset->as.material.name);
    asset->as.material.name = NULL;

    memset(&asset->as.material, 0, sizeof(asset->as.material));
}

static bool asset_material_save_blob(asset_manager_t *am, ihandle_t handle, const asset_any_t *asset, asset_blob_t *out_blob)
{
    (void)am;
    (void)handle;

    if (!asset || asset->type != ASSET_MATERIAL || !out_blob)
        return false;

    memset(out_blob, 0, sizeof(*out_blob));

    ikv_node_t *root = material_to_ikv(&asset->as.material, "material");
    if (!root)
        return false;

    void *bytes = NULL;
    uint32_t bytes_n = 0;

    bool ok = ikv_write_to_memory(root, &bytes, &bytes_n);
    ikv_free(root);

    if (!ok || !bytes || !bytes_n)
    {
        free(bytes);
        return false;
    }

    out_blob->data = bytes;
    out_blob->size = bytes_n;
    out_blob->uncompressed_size = bytes_n;
    out_blob->codec = 0;
    out_blob->flags = 0;
    out_blob->align = 64;

    return true;
}

static void asset_material_blob_free(asset_manager_t *am, asset_blob_t *blob)
{
    (void)am;

    if (!blob)
        return;

    free(blob->data);
    memset(blob, 0, sizeof(*blob));
}

asset_module_desc_t asset_module_material(void)
{
    asset_module_desc_t m;
    m.type = ASSET_MATERIAL;
    m.name = "ASSET_MATERIAL_KV1";
    m.load_fn = asset_material_load;
    m.init_fn = asset_material_init;
    m.cleanup_fn = asset_material_cleanup;
    m.save_blob_fn = asset_material_save_blob;
    m.blob_free_fn = asset_material_blob_free;
    return m;
}
