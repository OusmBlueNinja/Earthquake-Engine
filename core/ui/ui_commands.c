#include "ui_commands.h"
#include <string.h>

static void *ui_rs(ui_cmd_stream_t *s, void *ptr, uint32_t size)
{
    return s->rfn ? s->rfn(s->ruser, ptr, size) : 0;
}

static uint32_t ui_u32_max(uint32_t a, uint32_t b) { return a > b ? a : b; }

static void ui_cmd_grow(ui_cmd_stream_t *s, uint32_t need)
{
    if (s->cap >= need) return;
    uint32_t cap = s->cap ? s->cap : 256u;
    while (cap < need) cap *= 2u;
    void *p = ui_rs(s, s->cmds, cap * (uint32_t)sizeof(ui_cmd_t));
    if (!p) return;
    s->cmds = (ui_cmd_t *)p;
    s->cap = cap;
}

static void ui_str_grow(ui_cmd_stream_t *s, uint32_t need)
{
    if (s->str_cap >= need) return;
    uint32_t cap = s->str_cap ? s->str_cap : 1024u;
    while (cap < need) cap *= 2u;
    void *p = ui_rs(s, s->str, cap);
    if (!p) return;
    s->str = (char *)p;
    s->str_cap = cap;
}

static const char *ui_str_intern_n(ui_cmd_stream_t *s, const char *text, uint32_t n)
{
    if (!text) return "";
    uint32_t need = s->str_len + n + 1u;
    ui_str_grow(s, need);
    if (s->str_cap < need) return "";
    char *dst = s->str + s->str_len;
    memcpy(dst, text, n);
    dst[n] = 0;
    s->str_len += n + 1u;
    return dst;
}

static const char *ui_str_intern(ui_cmd_stream_t *s, const char *text)
{
    if (!text) return "";
    uint32_t n = (uint32_t)strlen(text);
    return ui_str_intern_n(s, text, n);
}

void ui_cmd_stream_init(ui_cmd_stream_t *s, ui_realloc_fn rfn, void *ruser)
{
    memset(s, 0, sizeof(*s));
    s->rfn = rfn;
    s->ruser = ruser;
}

void ui_cmd_stream_free(ui_cmd_stream_t *s)
{
    if (!s) return;
    if (s->cmds) ui_rs(s, s->cmds, 0);
    if (s->str) ui_rs(s, s->str, 0);
    memset(s, 0, sizeof(*s));
}

void ui_cmd_stream_reset(ui_cmd_stream_t *s)
{
    s->count = 0;
    s->str_len = 0;
}

uint32_t ui_cmd_count(const ui_cmd_stream_t *s) { return s ? s->count : 0; }
const ui_cmd_t *ui_cmd_data(const ui_cmd_stream_t *s) { return s ? s->cmds : 0; }

static ui_cmd_t *ui_cmd_push(ui_cmd_stream_t *s, ui_cmd_type_t type)
{
    uint32_t idx = s->count;
    ui_cmd_grow(s, idx + 1u);
    if (s->cap < idx + 1u) return 0;
    ui_cmd_t *c = &s->cmds[idx];
    memset(c, 0, sizeof(*c));
    c->type = type;
    s->count = idx + 1u;
    return c;
}

void ui_cmd_push_clip(ui_cmd_stream_t *s, ui_vec4_t rect)
{
    ui_cmd_t *c = ui_cmd_push(s, UI_CMD_CLIP);
    if (!c) return;
    c->clip.rect = rect;
}

void ui_cmd_push_rect(ui_cmd_stream_t *s, ui_vec4_t rect, ui_color_t color, float radius, float thickness)
{
    ui_cmd_t *c = ui_cmd_push(s, UI_CMD_RECT);
    if (!c) return;
    c->rect.rect = rect;
    c->rect.color = color;
    c->rect.radius = radius;
    c->rect.thickness = thickness;
}

void ui_cmd_push_text(ui_cmd_stream_t *s, ui_vec2_t pos, ui_color_t color, uint32_t font_id, const char *text)
{
    ui_cmd_t *c = ui_cmd_push(s, UI_CMD_TEXT);
    if (!c) return;
    c->text.pos = pos;
    c->text.color = color;
    c->text.font_id = font_id;
    c->text.text = ui_str_intern(s, text);
}

void ui_cmd_push_text_n(ui_cmd_stream_t *s, ui_vec2_t pos, ui_color_t color, uint32_t font_id, const char *text, uint32_t n)
{
    ui_cmd_t *c = ui_cmd_push(s, UI_CMD_TEXT);
    if (!c) return;
    c->text.pos = pos;
    c->text.color = color;
    c->text.font_id = font_id;
    c->text.text = ui_str_intern_n(s, text, n);
}

void ui_cmd_push_icon(ui_cmd_stream_t *s, ui_vec4_t rect, ui_color_t color, uint32_t font_id, uint32_t icon_id)
{
    ui_cmd_t *c = ui_cmd_push(s, UI_CMD_ICON);
    if (!c) return;
    c->icon.rect = rect;
    c->icon.color = color;
    c->icon.font_id = font_id;
    c->icon.icon_id = icon_id;
}

void ui_cmd_push_image(ui_cmd_stream_t *s, ui_vec4_t rect, uint32_t gl_tex, ui_vec4_t uv, ui_color_t tint)
{
    ui_cmd_t *c = ui_cmd_push(s, UI_CMD_IMAGE);
    if (!c) return;
    c->image.rect = rect;
    c->image.gl_tex = gl_tex;
    c->image.uv = uv;
    c->image.tint = tint;
}
