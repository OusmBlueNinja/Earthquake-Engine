#include "renderer/frame_graph.h"
#include <stdlib.h>
#include <string.h>

typedef struct fg_resource_t
{
    const char *name;
    fg_resource_type_t type;
    uint32_t gl_id;
} fg_resource_t;

typedef struct fg_use_t
{
    uint16_t res_id; // 1-based
    uint8_t access;  // fg_access_t
} fg_use_t;

typedef struct fg_pass_t
{
    const char *name;
    fg_execute_fn execute;
    void *user;

    fg_use_t *uses;
    uint16_t use_count;
    uint16_t use_cap;
} fg_pass_t;

struct frame_graph_t
{
    fg_resource_t *resources;
    uint16_t resource_count;
    uint16_t resource_cap;

    fg_pass_t *passes;
    uint16_t pass_count;
    uint16_t pass_cap;

    // edges[from * pass_cap + to] = 1
    uint8_t *edges;
    uint16_t order_count;
    uint16_t *order;
};

static uint16_t fg_u16_next_pow2(uint16_t x)
{
    if (x <= 1u)
        return 1u;
    uint32_t v = (uint32_t)x;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v++;
    if (v > 65535u)
        v = 65535u;
    return (uint16_t)v;
}

static void fg_grow_resources(frame_graph_t *fg, uint16_t need)
{
    if (fg->resource_cap >= need)
        return;
    uint16_t cap = fg->resource_cap ? fg->resource_cap : 64u;
    while (cap < need)
        cap = fg_u16_next_pow2((uint16_t)(cap + 1u));
    fg_resource_t *p = (fg_resource_t *)realloc(fg->resources, sizeof(fg_resource_t) * (size_t)cap);
    if (!p)
        return;
    fg->resources = p;
    fg->resource_cap = cap;
}

static void fg_grow_passes(frame_graph_t *fg, uint16_t need)
{
    if (fg->pass_cap >= need)
        return;
    uint16_t cap = fg->pass_cap ? fg->pass_cap : 32u;
    while (cap < need)
        cap = fg_u16_next_pow2((uint16_t)(cap + 1u));
    fg_pass_t *p = (fg_pass_t *)realloc(fg->passes, sizeof(fg_pass_t) * (size_t)cap);
    if (!p)
        return;
    memset(p + fg->pass_cap, 0, sizeof(fg_pass_t) * (size_t)(cap - fg->pass_cap));
    fg->passes = p;
    fg->pass_cap = cap;
}

static void fg_pass_grow_uses(fg_pass_t *p, uint16_t need)
{
    if (p->use_cap >= need)
        return;
    uint16_t cap = p->use_cap ? p->use_cap : 8u;
    while (cap < need)
        cap = fg_u16_next_pow2((uint16_t)(cap + 1u));
    fg_use_t *u = (fg_use_t *)realloc(p->uses, sizeof(fg_use_t) * (size_t)cap);
    if (!u)
        return;
    p->uses = u;
    p->use_cap = cap;
}

static void fg_rebuild_edges(frame_graph_t *fg)
{
    if (!fg)
        return;
    size_t n = (size_t)fg->pass_cap * (size_t)fg->pass_cap;
    uint8_t *e = (uint8_t *)realloc(fg->edges, n);
    if (!e)
        return;
    fg->edges = e;
    memset(fg->edges, 0, n);
}

frame_graph_t *fg_create(void)
{
    frame_graph_t *fg = (frame_graph_t *)calloc(1, sizeof(frame_graph_t));
    return fg;
}

void fg_destroy(frame_graph_t *fg)
{
    if (!fg)
        return;
    for (uint16_t i = 0; i < fg->pass_cap; ++i)
    {
        free(fg->passes[i].uses);
        fg->passes[i].uses = NULL;
        fg->passes[i].use_count = 0;
        fg->passes[i].use_cap = 0;
    }
    free(fg->passes);
    free(fg->resources);
    free(fg->edges);
    free(fg->order);
    free(fg);
}

void fg_begin(frame_graph_t *fg)
{
    if (!fg)
        return;
    fg->resource_count = 0;
    for (uint16_t i = 0; i < fg->pass_count; ++i)
    {
        fg->passes[i].name = NULL;
        fg->passes[i].execute = NULL;
        fg->passes[i].user = NULL;
        fg->passes[i].use_count = 0;
    }
    fg->pass_count = 0;
    fg->order_count = 0;
    if (fg->edges && fg->pass_cap)
        memset(fg->edges, 0, (size_t)fg->pass_cap * (size_t)fg->pass_cap);
}

static fg_handle_t fg_add_resource(frame_graph_t *fg, const char *name, fg_resource_type_t type, uint32_t gl_id)
{
    if (!fg)
        return fg_handle_invalid();
    uint16_t idx = fg->resource_count;
    fg_grow_resources(fg, (uint16_t)(idx + 1u));
    if (fg->resource_cap < (uint16_t)(idx + 1u))
        return fg_handle_invalid();
    fg->resources[idx] = (fg_resource_t){name ? name : "", type, gl_id};
    fg->resource_count = (uint16_t)(idx + 1u);
    return (fg_handle_t){(uint16_t)(idx + 1u)};
}

fg_handle_t fg_create_virtual(frame_graph_t *fg, const char *name)
{
    return fg_add_resource(fg, name, FG_RESOURCE_VIRTUAL, 0);
}

fg_handle_t fg_import_texture2d(frame_graph_t *fg, const char *name, uint32_t gl_tex)
{
    return fg_add_resource(fg, name, FG_RESOURCE_TEXTURE2D, gl_tex);
}

fg_handle_t fg_import_texture2d_array(frame_graph_t *fg, const char *name, uint32_t gl_tex)
{
    return fg_add_resource(fg, name, FG_RESOURCE_TEXTURE2D_ARRAY, gl_tex);
}

fg_handle_t fg_import_buffer(frame_graph_t *fg, const char *name, uint32_t gl_buf)
{
    return fg_add_resource(fg, name, FG_RESOURCE_BUFFER, gl_buf);
}

uint32_t fg_imported_gl_id(const frame_graph_t *fg, fg_handle_t h)
{
    if (!fg_handle_is_valid(h) || !fg)
        return 0;
    uint16_t idx = (uint16_t)(h.id - 1u);
    if (idx >= fg->resource_count)
        return 0;
    return fg->resources[idx].gl_id;
}

const char *fg_resource_name(const frame_graph_t *fg, fg_handle_t h)
{
    if (!fg_handle_is_valid(h) || !fg)
        return "";
    uint16_t idx = (uint16_t)(h.id - 1u);
    if (idx >= fg->resource_count)
        return "";
    return fg->resources[idx].name ? fg->resources[idx].name : "";
}

fg_resource_type_t fg_resource_type(const frame_graph_t *fg, fg_handle_t h)
{
    if (!fg_handle_is_valid(h) || !fg)
        return FG_RESOURCE_VIRTUAL;
    uint16_t idx = (uint16_t)(h.id - 1u);
    if (idx >= fg->resource_count)
        return FG_RESOURCE_VIRTUAL;
    return fg->resources[idx].type;
}

uint16_t fg_add_pass(frame_graph_t *fg, const char *name, fg_execute_fn execute, void *user)
{
    if (!fg || !execute)
        return 0;

    uint16_t idx = fg->pass_count;
    fg_grow_passes(fg, (uint16_t)(idx + 1u));
    if (fg->pass_cap < (uint16_t)(idx + 1u))
        return 0;

    if (fg->edges == NULL || fg->order == NULL || fg->pass_cap == (uint16_t)(idx + 1u))
    {
        fg_rebuild_edges(fg);
        fg->order = (uint16_t *)realloc(fg->order, sizeof(uint16_t) * (size_t)fg->pass_cap);
    }

    fg_pass_t *p = &fg->passes[idx];
    p->name = name ? name : "";
    p->execute = execute;
    p->user = user;
    p->use_count = 0;

    fg->pass_count = (uint16_t)(idx + 1u);
    return (uint16_t)(idx + 1u);
}

void fg_pass_use(frame_graph_t *fg, uint16_t pass_id, fg_handle_t res, fg_access_t access)
{
    if (!fg || pass_id == 0 || pass_id > fg->pass_count)
        return;
    if (!fg_handle_is_valid(res) || res.id > fg->resource_count)
        return;

    fg_pass_t *p = &fg->passes[(uint16_t)(pass_id - 1u)];
    uint16_t idx = p->use_count;
    fg_pass_grow_uses(p, (uint16_t)(idx + 1u));
    if (p->use_cap < (uint16_t)(idx + 1u))
        return;
    p->uses[idx] = (fg_use_t){res.id, (uint8_t)access};
    p->use_count = (uint16_t)(idx + 1u);
}

static void fg_add_edge(frame_graph_t *fg, uint16_t from0, uint16_t to0)
{
    if (!fg || !fg->edges)
        return;
    if (from0 >= fg->pass_count || to0 >= fg->pass_count)
        return;
    fg->edges[(size_t)from0 * (size_t)fg->pass_cap + (size_t)to0] = 1;
}

int fg_compile(frame_graph_t *fg)
{
    if (!fg)
        return 0;
    if (fg->pass_count == 0)
    {
        fg->order_count = 0;
        return 1;
    }

    if (!fg->edges || !fg->order)
        return 0;
    memset(fg->edges, 0, (size_t)fg->pass_cap * (size_t)fg->pass_cap);

    // Build edges based on simple hazard rules, plus a read-read chain per resource
    // for determinism and correctness around later writes.
    const int32_t pass_n = (int32_t)fg->pass_count;
    int32_t *last_writer = (int32_t *)malloc(sizeof(int32_t) * (size_t)fg->resource_count);
    int32_t *last_reader = (int32_t *)malloc(sizeof(int32_t) * (size_t)fg->resource_count);
    if (!last_writer || !last_reader)
    {
        free(last_writer);
        free(last_reader);
        return 0;
    }
    for (uint16_t r = 0; r < fg->resource_count; ++r)
    {
        last_writer[r] = -1;
        last_reader[r] = -1;
    }

    for (int32_t pi = 0; pi < pass_n; ++pi)
    {
        fg_pass_t *p = &fg->passes[pi];
        for (uint16_t ui = 0; ui < p->use_count; ++ui)
        {
            fg_use_t u = p->uses[ui];
            uint16_t ridx = (uint16_t)(u.res_id - 1u);
            if (ridx >= fg->resource_count)
                continue;

            if (u.access == FG_ACCESS_READ)
            {
                if (last_writer[ridx] >= 0)
                    fg_add_edge(fg, (uint16_t)last_writer[ridx], (uint16_t)pi);
                if (last_reader[ridx] >= 0)
                    fg_add_edge(fg, (uint16_t)last_reader[ridx], (uint16_t)pi);
                last_reader[ridx] = pi;
            }
            else
            {
                if (last_writer[ridx] >= 0)
                    fg_add_edge(fg, (uint16_t)last_writer[ridx], (uint16_t)pi);
                if (last_reader[ridx] >= 0)
                    fg_add_edge(fg, (uint16_t)last_reader[ridx], (uint16_t)pi);
                last_writer[ridx] = pi;
                last_reader[ridx] = -1;
            }
        }
    }

    free(last_writer);
    free(last_reader);

    // Kahn topo sort
    uint16_t *indeg = (uint16_t *)calloc((size_t)fg->pass_count, sizeof(uint16_t));
    uint16_t *queue = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)fg->pass_count);
    if (!indeg || !queue)
    {
        free(indeg);
        free(queue);
        return 0;
    }

    for (uint16_t from = 0; from < fg->pass_count; ++from)
        for (uint16_t to = 0; to < fg->pass_count; ++to)
            if (fg->edges[(size_t)from * (size_t)fg->pass_cap + (size_t)to])
                indeg[to] += 1u;

    uint16_t qh = 0, qt = 0;
    for (uint16_t i = 0; i < fg->pass_count; ++i)
        if (indeg[i] == 0)
            queue[qt++] = i;

    fg->order_count = 0;
    while (qh < qt)
    {
        uint16_t n = queue[qh++];
        fg->order[fg->order_count++] = n;
        for (uint16_t to = 0; to < fg->pass_count; ++to)
        {
            if (!fg->edges[(size_t)n * (size_t)fg->pass_cap + (size_t)to])
                continue;
            if (indeg[to] > 0)
                indeg[to] -= 1u;
            if (indeg[to] == 0)
                queue[qt++] = to;
        }
    }

    free(indeg);
    free(queue);

    return fg->order_count == fg->pass_count ? 1 : 0;
}

int fg_execute(frame_graph_t *fg)
{
    if (!fg)
        return 0;
    if (fg->pass_count == 0)
        return 1;
    if (fg->order_count != fg->pass_count)
        return 0;

    for (uint16_t i = 0; i < fg->order_count; ++i)
    {
        uint16_t pi = fg->order[i];
        if (pi >= fg->pass_count)
            continue;
        fg_pass_t *p = &fg->passes[pi];
        if (p->execute)
            p->execute(p->user);
    }

    return 1;
}

