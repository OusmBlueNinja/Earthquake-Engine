#include "opengl_backend.h"
#include <string.h>
#include <math.h>
#include "../ui_font.h"

#ifndef UI_Y_DOWN
#define UI_Y_DOWN 1
#endif

#define UI_PI 3.14159265358979323846f
#define UI_TAU (UI_PI * 2.0f)

typedef struct ui_gl_vtx_t
{
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
} ui_gl_vtx_t;

static float ui_gl_clampf(float v, float a, float b)
{
    if (v < a)
        return a;
    if (v > b)
        return b;
    return v;
}

static int ui_gl_clampi(int v, int a, int b)
{
    if (v < a)
        return a;
    if (v > b)
        return b;
    return v;
}

static float ui_gl_minf(float a, float b)
{
    return a < b ? a : b;
}

static GLuint ui_gl_compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint ui_gl_link(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static uint32_t ui_utf8_next_n(const char **p, const char *end)
{
    const uint8_t *s = (const uint8_t *)(*p);
    if (!s || *p >= end)
        return 0;

    uint8_t b0 = s[0];

    if (b0 < 0x80)
    {
        *p += 1;
        return (uint32_t)b0;
    }

    if ((b0 & 0xE0) == 0xC0)
    {
        if (*p + 2 > end)
        {
            *p = end;
            return 0;
        }
        uint8_t b1 = s[1];
        if ((b1 & 0xC0) != 0x80)
        {
            *p += 1;
            return 0xFFFD;
        }
        uint32_t cp = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
        *p += 2;
        if (cp < 0x80)
            return 0xFFFD;
        return cp;
    }

    if ((b0 & 0xF0) == 0xE0)
    {
        if (*p + 3 > end)
        {
            *p = end;
            return 0;
        }
        uint8_t b1 = s[1], b2 = s[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80)
        {
            *p += 1;
            return 0xFFFD;
        }
        uint32_t cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(b1 & 0x3F) << 6) | (uint32_t)(b2 & 0x3F);
        *p += 3;
        if (cp < 0x800)
            return 0xFFFD;
        if (cp >= 0xD800 && cp <= 0xDFFF)
            return 0xFFFD;
        return cp;
    }

    if ((b0 & 0xF8) == 0xF0)
    {
        if (*p + 4 > end)
        {
            *p = end;
            return 0;
        }
        uint8_t b1 = s[1], b2 = s[2], b3 = s[3];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
        {
            *p += 1;
            return 0xFFFD;
        }
        uint32_t cp = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(b1 & 0x3F) << 12) | ((uint32_t)(b2 & 0x3F) << 6) | (uint32_t)(b3 & 0x3F);
        *p += 4;
        if (cp < 0x10000 || cp > 0x10FFFF)
            return 0xFFFD;
        return cp;
    }

    *p += 1;
    return 0xFFFD;
}

static uint32_t ui_utf8_next(const char **p)
{
    const char *end = *p;
    while (*end)
        end++;
    return ui_utf8_next_n(p, end);
}

static ui_vec4_t ui_gl_full_clip(const ui_gl_backend_t *b)
{
    return ui_v4(0.0f, 0.0f, (float)b->fb_w, (float)b->fb_h);
}

static void ui_gl_set_scissor(ui_gl_backend_t *b, ui_vec4_t clip)
{
    if (clip.z < 0.0f)
        clip.z = 0.0f;
    if (clip.w < 0.0f)
        clip.w = 0.0f;

    int x = (int)(clip.x + 0.5f);
    int y = (int)(clip.y + 0.5f);
    int w = (int)(clip.z + 0.5f);
    int h = (int)(clip.w + 0.5f);

    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;

    int sy = b->fb_h - (y + h);
    if (sy < 0)
        sy = 0;

    if (!b->scissor_enabled)
    {
        glEnable(GL_SCISSOR_TEST);
        b->scissor_enabled = 1;
    }

    glScissor(x, sy, w, h);
    b->cur_clip = clip;
}

static void ui_gl_flush(ui_gl_backend_t *b)
{
    uint32_t n = ui_array_count(&b->verts);
    if (!n)
        return;

    if (!b->prog || !b->vao || !b->vbo)
    {
        ui_array_clear(&b->verts);
        return;
    }

    if (b->cur_tex == 0)
        b->cur_tex = b->white_tex;

    glUseProgram(b->prog);
    glUniform2f(b->u_screen, (float)b->fb_w, (float)b->fb_h);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)b->cur_tex);
    glUniform1i(b->u_tex, 0);

    glBindVertexArray(b->vao);
    glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n * (GLsizeiptr)sizeof(ui_gl_vtx_t), ui_array_data(&b->verts), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)n);

    ui_array_clear(&b->verts);
}

static void ui_gl_set_tex(ui_gl_backend_t *b, uint32_t tex)
{
    if (tex == 0)
        tex = b->white_tex;

    if (b->cur_tex == tex)
        return;

    ui_gl_flush(b);
    b->cur_tex = tex;
}

const ui_gl_font_t *ui_gl_backend_get_font(const ui_gl_backend_t *b, uint32_t font_id)
{
    if (font_id >= ui_array_count(&b->fonts))
        return 0;
    return (const ui_gl_font_t *)ui_array_at((ui_array_t *)&b->fonts, font_id);
}

static ui_gl_font_t *ui_gl_get_font(ui_gl_backend_t *b, uint32_t font_id)
{
    if (font_id >= ui_array_count(&b->fonts))
        return 0;
    return (ui_gl_font_t *)ui_array_at(&b->fonts, font_id);
}

static int ui_gl_reserve_verts(ui_gl_backend_t *b, uint32_t add)
{
    uint32_t n = ui_array_count(&b->verts);
    uint32_t need = n + add;
    return ui_array_reserve(&b->verts, need) ? 1 : 0;
}

static void ui_gl_push_tri(ui_gl_backend_t *b, ui_gl_vtx_t a, ui_gl_vtx_t c, ui_gl_vtx_t d)
{
    if (!ui_gl_reserve_verts(b, 3))
        return;

    uint32_t n = ui_array_count(&b->verts);
    ui_gl_vtx_t *dst = (ui_gl_vtx_t *)((uint8_t *)ui_array_data(&b->verts) + (size_t)n * (size_t)b->verts.stride);

    dst[0] = a;
    dst[1] = c;
    dst[2] = d;

    b->verts.count = n + 3;
}

static void ui_gl_push_quad(ui_gl_backend_t *b, float x, float y, float w, float h, float u0, float v0, float u1, float v1, ui_color_t col)
{
    ui_gl_vtx_t a, c, d, e;
    a.x = x;
    a.y = y;
    a.u = u0;
    a.v = v0;
    a.r = col.rgb.x;
    a.g = col.rgb.y;
    a.b = col.rgb.z;
    a.a = col.a;

    c.x = x + w;
    c.y = y;
    c.u = u1;
    c.v = v0;
    c.r = col.rgb.x;
    c.g = col.rgb.y;
    c.b = col.rgb.z;
    c.a = col.a;

    d.x = x + w;
    d.y = y + h;
    d.u = u1;
    d.v = v1;
    d.r = col.rgb.x;
    d.g = col.rgb.y;
    d.b = col.rgb.z;
    d.a = col.a;

    e.x = x;
    e.y = y + h;
    e.u = u0;
    e.v = v1;
    e.r = col.rgb.x;
    e.g = col.rgb.y;
    e.b = col.rgb.z;
    e.a = col.a;

    ui_gl_push_tri(b, a, c, d);
    ui_gl_push_tri(b, a, d, e);
}

static ui_gl_vtx_t ui_gl_make_vtx(float x, float y, float u, float v, ui_color_t col)
{
    ui_gl_vtx_t t;
    t.x = x;
    t.y = y;
    t.u = u;
    t.v = v;
    t.r = col.rgb.x;
    t.g = col.rgb.y;
    t.b = col.rgb.z;
    t.a = col.a;
    return t;
}

static void ui_gl_push_fan(ui_gl_backend_t *b, float cx, float cy, float r, float a0, float a1, int segs, ui_color_t col)
{
    if (r <= 0.0f || segs < 1)
        return;

    if (a1 < a0)
    {
        float t = a0;
        a0 = a1;
        a1 = t;
    }

    float da = (a1 - a0) / (float)segs;

    ui_gl_vtx_t vc = ui_gl_make_vtx(cx, cy, 0.0f, 0.0f, col);

    float a = a0;
    float x0 = cx + cosf(a) * r;
#if UI_Y_DOWN
    float y0 = cy - sinf(a) * r;
#else
    float y0 = cy + sinf(a) * r;
#endif
    ui_gl_vtx_t v0 = ui_gl_make_vtx(x0, y0, 0.0f, 0.0f, col);

    for (int i = 1; i <= segs; ++i)
    {
        a = a0 + da * (float)i;
        float x1 = cx + cosf(a) * r;
#if UI_Y_DOWN
        float y1 = cy - sinf(a) * r;
#else
        float y1 = cy + sinf(a) * r;
#endif
        ui_gl_vtx_t v1 = ui_gl_make_vtx(x1, y1, 0.0f, 0.0f, col);
        ui_gl_push_tri(b, vc, v0, v1);
        v0 = v1;
    }
}

static void ui_gl_push_ring_sector(ui_gl_backend_t *b, float cx, float cy, float r0, float r1, float a0, float a1, int segs, ui_color_t col)
{
    if (r0 <= 0.0f || r1 <= 0.0f || segs < 1)
        return;

    if (a1 < a0)
    {
        float t = a0;
        a0 = a1;
        a1 = t;
    }

    float da = (a1 - a0) / (float)segs;

    float a = a0;

    float ox0 = cx + cosf(a) * r0;
    float ix0 = cx + cosf(a) * r1;
#if UI_Y_DOWN
    float oy0 = cy - sinf(a) * r0;
    float iy0 = cy - sinf(a) * r1;
#else
    float oy0 = cy + sinf(a) * r0;
    float iy0 = cy + sinf(a) * r1;
#endif

    ui_gl_vtx_t o0 = ui_gl_make_vtx(ox0, oy0, 0.0f, 0.0f, col);
    ui_gl_vtx_t i0 = ui_gl_make_vtx(ix0, iy0, 0.0f, 0.0f, col);

    for (int i = 1; i <= segs; ++i)
    {
        a = a0 + da * (float)i;

        float ox1 = cx + cosf(a) * r0;
        float ix1 = cx + cosf(a) * r1;
#if UI_Y_DOWN
        float oy1 = cy - sinf(a) * r0;
        float iy1 = cy - sinf(a) * r1;
#else
        float oy1 = cy + sinf(a) * r0;
        float iy1 = cy + sinf(a) * r1;
#endif

        ui_gl_vtx_t o1 = ui_gl_make_vtx(ox1, oy1, 0.0f, 0.0f, col);
        ui_gl_vtx_t i1 = ui_gl_make_vtx(ix1, iy1, 0.0f, 0.0f, col);

        ui_gl_push_tri(b, o0, i0, i1);
        ui_gl_push_tri(b, o0, i1, o1);

        o0 = o1;
        i0 = i1;
    }
}

static int ui_gl_round_segs(float r)
{
    int s = (int)(r * 0.5f + 6.0f);
    return ui_gl_clampi(s, 6, 32);
}

static void ui_gl_draw_rect_filled_rounded(ui_gl_backend_t *b, ui_vec4_t rr, float radius, ui_color_t col)
{
    float x = rr.x;
    float y = rr.y;
    float w = rr.z;
    float h = rr.w;

    if (w <= 0.0f || h <= 0.0f)
        return;

    float r = radius;
    float mr = ui_gl_minf(w, h) * 0.5f;
    r = ui_gl_clampf(r, 0.0f, mr);

    if (r <= 0.001f)
    {
        ui_gl_push_quad(b, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, col);
        return;
    }

    float cw = w - 2.0f * r;
    float ch = h - 2.0f * r;

    if (cw > 0.0f && ch > 0.0f)
        ui_gl_push_quad(b, x + r, y + r, cw, ch, 0.0f, 0.0f, 1.0f, 1.0f, col);

    if (cw > 0.0f)
    {
        ui_gl_push_quad(b, x + r, y, cw, r, 0.0f, 0.0f, 1.0f, 1.0f, col);
        ui_gl_push_quad(b, x + r, y + h - r, cw, r, 0.0f, 0.0f, 1.0f, 1.0f, col);
    }

    if (ch > 0.0f)
    {
        ui_gl_push_quad(b, x, y + r, r, ch, 0.0f, 0.0f, 1.0f, 1.0f, col);
        ui_gl_push_quad(b, x + w - r, y + r, r, ch, 0.0f, 0.0f, 1.0f, 1.0f, col);
    }

    int segs = ui_gl_round_segs(r);

    float ctrx = x + w - r;
    float ctry = y + r;
    ui_gl_push_fan(b, ctrx, ctry, r, 0.0f, UI_PI * 0.5f, segs, col);

    float ctlx = x + r;
    float ctly = y + r;
    ui_gl_push_fan(b, ctlx, ctly, r, UI_PI * 0.5f, UI_PI, segs, col);

    float cblx = x + r;
    float cbly = y + h - r;
    ui_gl_push_fan(b, cblx, cbly, r, UI_PI, UI_PI * 1.5f, segs, col);

    float cbrx = x + w - r;
    float cbry = y + h - r;
    ui_gl_push_fan(b, cbrx, cbry, r, UI_PI * 1.5f, UI_TAU, segs, col);
}

static void ui_gl_draw_rect_stroke_rounded(ui_gl_backend_t *b, ui_vec4_t rr, float radius, float thickness, ui_color_t col)
{
    float x = rr.x;
    float y = rr.y;
    float w = rr.z;
    float h = rr.w;

    if (w <= 0.0f || h <= 0.0f)
        return;

    float t = thickness;
    if (t <= 0.0f)
        return;

    float mr = ui_gl_minf(w, h) * 0.5f;
    float r = ui_gl_clampf(radius, 0.0f, mr);

    if (r <= 0.001f)
    {
        ui_gl_push_quad(b, x, y, w, t, 0.0f, 0.0f, 1.0f, 1.0f, col);
        ui_gl_push_quad(b, x, y + h - t, w, t, 0.0f, 0.0f, 1.0f, 1.0f, col);
        if (h - 2.0f * t > 0.0f)
        {
            ui_gl_push_quad(b, x, y + t, t, h - 2.0f * t, 0.0f, 0.0f, 1.0f, 1.0f, col);
            ui_gl_push_quad(b, x + w - t, y + t, t, h - 2.0f * t, 0.0f, 0.0f, 1.0f, 1.0f, col);
        }
        return;
    }

    float max_t = mr;
    t = ui_gl_clampf(t, 0.0f, max_t);

    float inner_r = r - t;
    if (inner_r < 0.0f)
        inner_r = 0.0f;

    float top_w = w - 2.0f * r;
    float side_h = h - 2.0f * r;

    if (top_w > 0.0f)
    {
        ui_gl_push_quad(b, x + r, y, top_w, t, 0.0f, 0.0f, 1.0f, 1.0f, col);
        ui_gl_push_quad(b, x + r, y + h - t, top_w, t, 0.0f, 0.0f, 1.0f, 1.0f, col);
    }

    if (side_h > 0.0f)
    {
        ui_gl_push_quad(b, x, y + r, t, side_h, 0.0f, 0.0f, 1.0f, 1.0f, col);
        ui_gl_push_quad(b, x + w - t, y + r, t, side_h, 0.0f, 0.0f, 1.0f, 1.0f, col);
    }

    int segs = ui_gl_round_segs(r);

    float ctrx = x + w - r;
    float ctry = y + r;
    ui_gl_push_ring_sector(b, ctrx, ctry, r, inner_r, 0.0f, UI_PI * 0.5f, segs, col);

    float ctlx = x + r;
    float ctly = y + r;
    ui_gl_push_ring_sector(b, ctlx, ctly, r, inner_r, UI_PI * 0.5f, UI_PI, segs, col);

    float cblx = x + r;
    float cbly = y + h - r;
    ui_gl_push_ring_sector(b, cblx, cbly, r, inner_r, UI_PI, UI_PI * 1.5f, segs, col);

    float cbrx = x + w - r;
    float cbry = y + h - r;
    ui_gl_push_ring_sector(b, cbrx, cbry, r, inner_r, UI_PI * 1.5f, UI_TAU, segs, col);
}

static void ui_gl_draw_rect(ui_gl_backend_t *b, const ui_cmd_rect_t *r)
{
    ui_gl_set_tex(b, b->white_tex);

    ui_vec4_t rr = r->rect;

    if (r->thickness <= 0.0f)
    {
        ui_gl_draw_rect_filled_rounded(b, rr, r->radius, r->color);
        return;
    }

    ui_gl_draw_rect_stroke_rounded(b, rr, r->radius, r->thickness, r->color);
}

static void ui_gl_draw_image(ui_gl_backend_t *b, const ui_cmd_image_t *im)
{
    ui_gl_set_tex(b, im->gl_tex);
    ui_gl_push_quad(b, im->rect.x, im->rect.y, im->rect.z, im->rect.w, im->uv.x, im->uv.y, im->uv.z, im->uv.w, im->tint);
}

static void ui_gl_font_sync_texture(ui_gl_backend_t *b, ui_gl_font_t *f)
{
    if (!f || !f->used || !f->tex || !f->font.rgba)
        return;

    if (!f->font.dirty)
        return;

    ui_gl_flush(b);

    glBindTexture(GL_TEXTURE_2D, (GLuint)f->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (f->tex_w != f->font.atlas_w || f->tex_h != f->font.atlas_h)
    {
        f->tex_w = f->font.atlas_w;
        f->tex_h = f->font.atlas_h;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, f->tex_w, f->tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, f->font.rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->tex_w, f->tex_h, GL_RGBA, GL_UNSIGNED_BYTE, f->font.rgba);
    }

    f->font.dirty = 0;
}

static void ui_gl_draw_glyph(ui_gl_backend_t *b, ui_gl_font_t *f, float x, float baseline_y, uint32_t cp, ui_color_t col)
{
    const ui_glyph_t *g = ui_font_get_glyph(&f->font, cp);
    if (!g)
        return;

    ui_gl_font_sync_texture(b, f);

    float w = (float)(g->w ? g->w : 1);
    float h = (float)(g->h ? g->h : 1);

    float px = x + (float)g->xoff;
    float py = baseline_y + (float)g->yoff;

    ui_gl_set_tex(b, f->tex);
    ui_gl_push_quad(b, px, py, w, h, g->u0, g->v0, g->u1, g->v1, col);
}

static void ui_gl_draw_text(ui_gl_backend_t *b, const ui_cmd_text_t *t)
{
    ui_gl_font_t *f = ui_gl_get_font(b, t->font_id);
    if (!f || !f->used || !f->tex || !t->text)
        return;

    float x = t->pos.x;
    float y = t->pos.y;

    float baseline = y + f->ascent_px;

    const char *p = t->text;
    for (;;)
    {
        uint32_t cp = ui_utf8_next(&p);
        if (!cp)
            break;

        if (cp == '\n')
        {
            x = t->pos.x;
            y += f->line_h;
            baseline = y + f->ascent_px;
            continue;
        }
        if (cp == '\r')
            continue;
        if (cp == '\t')
        {
            x += f->line_h;
            continue;
        }

        const ui_glyph_t *g = ui_font_get_glyph(&f->font, cp);
        if (g)
        {
            ui_gl_draw_glyph(b, f, x, baseline, cp, t->color);
            x += g->xadvance;
        }
    }
}

static void ui_gl_draw_icon(ui_gl_backend_t *b, const ui_cmd_icon_t *ic)
{
    ui_gl_font_t *f = ui_gl_get_font(b, ic->font_id);
    if (!f || !f->used || !f->tex)
        return;

    ui_gl_font_sync_texture(b, f);

    const ui_glyph_t *g = ui_font_get_glyph(&f->font, ic->icon_id);
    if (!g)
        return;

    float gw = (float)(g->w ? g->w : 1);
    float gh = (float)(g->h ? g->h : 1);

    float scale_x = ic->rect.z / (gw > 0.0f ? gw : 1.0f);
    float scale_y = ic->rect.w / (gh > 0.0f ? gh : 1.0f);
    float scale = scale_x < scale_y ? scale_x : scale_y;

    float w = gw * scale;
    float h = gh * scale;

    float px = ic->rect.x + (ic->rect.z - w) * 0.5f;
    float py = ic->rect.y + (ic->rect.w - h) * 0.5f;

    ui_gl_set_tex(b, f->tex);
    ui_gl_push_quad(b, px, py, w, h, g->u0, g->v0, g->u1, g->v1, ic->color);
}

static void ui_gl_base_begin(ui_base_backend_t *base, int fb_w, int fb_h)
{
    ui_gl_backend_t *b = (ui_gl_backend_t *)base->user;

    b->fb_w = fb_w;
    b->fb_h = fb_h;

    ui_array_clear(&b->verts);
    b->cur_tex = b->white_tex;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLint sample_buffers = 0;
    GLint samples = 0;
    glGetIntegerv(GL_SAMPLE_BUFFERS, &sample_buffers);
    glGetIntegerv(GL_SAMPLES, &samples);

    if (sample_buffers > 0 && samples > 0)
        glEnable(GL_MULTISAMPLE);
    else
        glDisable(GL_MULTISAMPLE);

    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);

    ui_gl_set_scissor(b, ui_gl_full_clip(b));
}

static void ui_gl_base_render(ui_base_backend_t *base, const ui_cmd_t *cmds, uint32_t count)
{
    ui_gl_backend_t *b = (ui_gl_backend_t *)base->user;

    for (uint32_t i = 0; i < count; ++i)
    {
        const ui_cmd_t *c = &cmds[i];

        if (c->type == UI_CMD_CLIP)
        {
            ui_gl_flush(b);
            ui_gl_set_scissor(b, c->clip.rect);
        }
        else if (c->type == UI_CMD_RECT)
        {
            ui_gl_draw_rect(b, &c->rect);
        }
        else if (c->type == UI_CMD_TEXT)
        {
            ui_gl_draw_text(b, &c->text);
        }
        else if (c->type == UI_CMD_ICON)
        {
            ui_gl_draw_icon(b, &c->icon);
        }
        else if (c->type == UI_CMD_IMAGE)
        {
            ui_gl_draw_image(b, &c->image);
        }
    }

    ui_gl_flush(b);
}

static void ui_gl_base_end(ui_base_backend_t *base)
{
    ui_gl_backend_t *b = (ui_gl_backend_t *)base->user;

    ui_gl_flush(b);

    if (b->scissor_enabled)
    {
        glDisable(GL_SCISSOR_TEST);
        b->scissor_enabled = 0;
    }

    glDisable(GL_BLEND);
    glDisable(GL_MULTISAMPLE);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

ui_base_backend_t *ui_gl_backend_base(ui_gl_backend_t *b)
{
    return &b->base;
}

int ui_gl_backend_text_width(void *user, uint32_t font_id, const char *text, int len)
{
    ui_gl_backend_t *b = (ui_gl_backend_t *)user;
    ui_gl_font_t *f = ui_gl_get_font(b, font_id);
    if (!f || !f->used || !text)
        return 0;

    float x = 0.0f;
    float best = 0.0f;

    if (len < 0)
    {
        float w = 0.0f, h = 0.0f;
        ui_font_measure_utf8(&f->font, text, &w, &h);
        ui_gl_font_sync_texture(b, f);
        return (int)(w + 0.5f);
    }

    const char *p = text;
    const char *end = text + len;

    for (;;)
    {
        uint32_t cp = ui_utf8_next_n(&p, end);
        if (!cp)
            break;

        if (cp == '\n')
        {
            if (x > best)
                best = x;
            x = 0.0f;
            continue;
        }
        if (cp == '\r')
            continue;
        if (cp == '\t')
        {
            x += f->line_h;
            continue;
        }

        const ui_glyph_t *g = ui_font_get_glyph(&f->font, cp);
        if (g)
            x += g->xadvance;
    }

    if (x > best)
        best = x;
    ui_gl_font_sync_texture(b, f);
    return (int)(best + 0.5f);
}

float ui_gl_backend_text_height(void *user, uint32_t font_id)
{
    ui_gl_backend_t *b = (ui_gl_backend_t *)user;
    ui_gl_font_t *f = ui_gl_get_font(b, font_id);
    if (!f || !f->used)
        return 0.0f;
    ui_gl_font_sync_texture(b, f);
    return f->line_h;
}

int ui_gl_backend_init(ui_gl_backend_t *b, ui_realloc_fn rfn, void *ruser)
{
    memset(b, 0, sizeof(*b));
    b->rfn = rfn;
    b->ruser = ruser;

    ui_array_init(&b->verts, (uint32_t)sizeof(ui_gl_vtx_t), rfn, ruser);
    ui_array_init(&b->fonts, (uint32_t)sizeof(ui_gl_font_t), rfn, ruser);

    const char *vs_src =
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "layout(location=2) in vec4 aCol;\n"
        "uniform vec2 uScreen;\n"
        "out vec2 vUV;\n"
        "out vec4 vCol;\n"
        "void main(){\n"
        "  vec2 ndc = vec2((aPos.x / uScreen.x) * 2.0 - 1.0, 1.0 - (aPos.y / uScreen.y) * 2.0);\n"
        "  gl_Position = vec4(ndc, 0.0, 1.0);\n"
        "  vUV = aUV;\n"
        "  vCol = aCol;\n"
        "}\n";

    const char *fs_src =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "in vec4 vCol;\n"
        "uniform sampler2D uTex;\n"
        "out vec4 oCol;\n"
        "void main(){\n"
        "  vec4 t = texture(uTex, vUV);\n"
        "  oCol = t * vCol;\n"
        "}\n";

    GLuint vs = ui_gl_compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = ui_gl_compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs)
        return 0;

    b->prog = ui_gl_link(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!b->prog)
        return 0;

    b->u_screen = glGetUniformLocation(b->prog, "uScreen");
    b->u_tex = glGetUniformLocation(b->prog, "uTex");

    glGenVertexArrays(1, &b->vao);
    glGenBuffers(1, &b->vbo);

    glBindVertexArray(b->vao);
    glBindBuffer(GL_ARRAY_BUFFER, b->vbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(ui_gl_vtx_t), (void *)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(ui_gl_vtx_t), (void *)(sizeof(float) * 2));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(ui_gl_vtx_t), (void *)(sizeof(float) * 4));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glGenTextures(1, &b->white_tex);
    glBindTexture(GL_TEXTURE_2D, b->white_tex);
    uint32_t px = 0xFFFFFFFFu;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    b->cur_tex = b->white_tex;
    b->cur_clip = ui_v4(0, 0, 0, 0);
    b->scissor_enabled = 0;

    b->base.user = b;
    b->base.text_width = ui_gl_backend_text_width;
    b->base.text_height = ui_gl_backend_text_height;
    b->base.begin = ui_gl_base_begin;
    b->base.render = ui_gl_base_render;
    b->base.end = ui_gl_base_end;

    return 1;
}

void ui_gl_backend_shutdown(ui_gl_backend_t *b)
{
    ui_gl_flush(b);

    uint32_t n = ui_array_count(&b->fonts);
    for (uint32_t i = 0; i < n; ++i)
    {
        ui_gl_font_t *f = (ui_gl_font_t *)ui_array_at(&b->fonts, i);
        if (f && f->used)
        {
            if (f->tex)
            {
                GLuint t = (GLuint)f->tex;
                glDeleteTextures(1, &t);
            }
            ui_font_free(&f->font);
        }
    }

    if (b->white_tex)
        glDeleteTextures(1, &b->white_tex);
    if (b->vbo)
        glDeleteBuffers(1, &b->vbo);
    if (b->vao)
        glDeleteVertexArrays(1, &b->vao);
    if (b->prog)
        glDeleteProgram(b->prog);

    ui_array_free(&b->verts);
    ui_array_free(&b->fonts);

    memset(b, 0, sizeof(*b));
}

static uint32_t ui_gl_backend_upload_rgba_texture(int w, int h, const uint8_t *rgba)
{
    if (w <= 0 || h <= 0 || !rgba)
        return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return (uint32_t)tex;
}

static int ui_gl_backend_set_font(ui_gl_backend_t *b, uint32_t font_id, ui_gl_font_t font)
{
    uint32_t need = font_id + 1;
    if (!ui_array_reserve(&b->fonts, need))
        return 0;

    while (ui_array_count(&b->fonts) < need)
    {
        ui_gl_font_t *f = (ui_gl_font_t *)ui_array_push(&b->fonts);
        if (!f)
            return 0;
        memset(f, 0, sizeof(*f));
    }

    ui_gl_font_t *dst = (ui_gl_font_t *)ui_array_at(&b->fonts, font_id);

    if (dst->used)
    {
        if (dst->tex)
        {
            GLuint t = (GLuint)dst->tex;
            glDeleteTextures(1, &t);
        }
        ui_font_free(&dst->font);
        memset(dst, 0, sizeof(*dst));
    }

    *dst = font;
    return 1;
}

int ui_gl_backend_add_font_from_ttf_file(ui_gl_backend_t *b, uint32_t font_id, const char *path, float px_height)
{
    if (!b || !path)
        return 0;

    ui_font_t f;
    if (!ui_font_load_ttf_file(&f, b->rfn, b->ruser, path, px_height))
        return 0;

    ui_font_preload_common(&f);

    uint32_t tex = ui_gl_backend_upload_rgba_texture(f.atlas_w, f.atlas_h, f.rgba);
    if (!tex)
    {
        ui_font_free(&f);
        return 0;
    }

    ui_gl_font_t gf;
    memset(&gf, 0, sizeof(gf));
    gf.used = 1;
    gf.tex = tex;
    gf.tex_w = f.atlas_w;
    gf.tex_h = f.atlas_h;
    gf.font = f;

    gf.ascent_px = (float)gf.font.ascent * gf.font.scale;
    gf.line_h = (float)(gf.font.ascent - gf.font.descent + gf.font.linegap) * gf.font.scale;
    if (gf.line_h < gf.font.px_height)
        gf.line_h = gf.font.px_height;

    b->cur_tex = b->white_tex;

    return ui_gl_backend_set_font(b, font_id, gf);
}

int ui_gl_backend_add_font_from_ttf_memory(ui_gl_backend_t *b, uint32_t font_id, const void *data, uint32_t size, float px_height)
{
    if (!b || !data || size == 0)
        return 0;

    ui_font_t f;
    if (!ui_font_load_ttf_memory(&f, b->rfn, b->ruser, data, size, px_height))
        return 0;

    ui_font_preload_common(&f);

    uint32_t tex = ui_gl_backend_upload_rgba_texture(f.atlas_w, f.atlas_h, f.rgba);
    if (!tex)
    {
        ui_font_free(&f);
        return 0;
    }

    ui_gl_font_t gf;
    memset(&gf, 0, sizeof(gf));
    gf.used = 1;
    gf.tex = tex;
    gf.tex_w = f.atlas_w;
    gf.tex_h = f.atlas_h;
    gf.font = f;

    gf.ascent_px = (float)gf.font.ascent * gf.font.scale;
    gf.line_h = (float)(gf.font.ascent - gf.font.descent + gf.font.linegap) * gf.font.scale;
    if (gf.line_h < gf.font.px_height)
        gf.line_h = gf.font.px_height;

    b->cur_tex = b->white_tex;

    return ui_gl_backend_set_font(b, font_id, gf);
}
