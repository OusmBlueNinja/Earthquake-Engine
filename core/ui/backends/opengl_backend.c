#include "opengl_backend.h"
#include <string.h>
#include "../ui_font.h"

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

    int sy = b->fb_h - (y + h);

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

static void ui_gl_push_tri(ui_gl_backend_t *b, ui_gl_vtx_t a, ui_gl_vtx_t c, ui_gl_vtx_t d)
{
    ui_gl_vtx_t *p0 = (ui_gl_vtx_t *)ui_array_push(&b->verts);
    ui_gl_vtx_t *p1 = (ui_gl_vtx_t *)ui_array_push(&b->verts);
    ui_gl_vtx_t *p2 = (ui_gl_vtx_t *)ui_array_push(&b->verts);
    if (!p0 || !p1 || !p2)
        return;
    *p0 = a;
    *p1 = c;
    *p2 = d;
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

static void ui_gl_draw_rect(ui_gl_backend_t *b, const ui_cmd_rect_t *r)
{
    ui_gl_set_tex(b, b->white_tex);

    if (r->thickness <= 0.0f)
    {
        ui_gl_push_quad(b, r->rect.x, r->rect.y, r->rect.z, r->rect.w, 0.0f, 0.0f, 1.0f, 1.0f, r->color);
        return;
    }

    float t = r->thickness;
    float x = r->rect.x;
    float y = r->rect.y;
    float w = r->rect.z;
    float h = r->rect.w;

    ui_gl_push_quad(b, x, y, w, t, 0.0f, 0.0f, 1.0f, 1.0f, r->color);
    ui_gl_push_quad(b, x, y + h - t, w, t, 0.0f, 0.0f, 1.0f, 1.0f, r->color);
    ui_gl_push_quad(b, x, y + t, t, h - 2.0f * t, 0.0f, 0.0f, 1.0f, 1.0f, r->color);
    ui_gl_push_quad(b, x + w - t, y + t, t, h - 2.0f * t, 0.0f, 0.0f, 1.0f, 1.0f, r->color);
}

static void ui_gl_draw_image(ui_gl_backend_t *b, const ui_cmd_image_t *im)
{
    ui_gl_set_tex(b, im->gl_tex);
    ui_gl_push_quad(b, im->rect.x, im->rect.y, im->rect.z, im->rect.w, im->uv.x, im->uv.y, im->uv.z, im->uv.w, im->tint);
}

static void ui_gl_draw_glyph_grid(ui_gl_backend_t *b, const ui_gl_font_t *f, float x, float y, uint32_t codepoint, ui_color_t col, float w, float h)
{
    if (!f || !f->tex)
        return;
    if (codepoint < f->first_char)
        return;
    uint32_t idx = codepoint - f->first_char;
    if (idx >= f->char_count)
        return;

    int col_i = (int)(idx % (uint32_t)f->cols);
    int row_i = (int)(idx / (uint32_t)f->cols);

    float u0 = (float)col_i / (float)f->cols;
    float v0 = (float)row_i / (float)f->rows;
    float u1 = (float)(col_i + 1) / (float)f->cols;
    float v1 = (float)(row_i + 1) / (float)f->rows;

    ui_gl_set_tex(b, f->tex);
    ui_gl_push_quad(b, x, y, w, h, u0, v0, u1, v1, col);
}

static void ui_gl_draw_text(ui_gl_backend_t *b, const ui_cmd_text_t *t)
{
    ui_gl_font_t *f = ui_gl_get_font(b, t->font_id);
    if (!f || !f->tex || !t->text)
        return;

    float advx = f->advance_x > 0.0f ? f->advance_x : (float)f->cell_w;
    float advy = f->advance_y > 0.0f ? f->advance_y : (float)f->cell_h;

    float x = t->pos.x;
    float y = t->pos.y;

    const unsigned char *s = (const unsigned char *)t->text;
    while (*s)
    {
        unsigned char ch = *s++;
        if (ch == '\n')
        {
            x = t->pos.x;
            y += advy;
            continue;
        }
        if (ch == '\r')
            continue;
        if (ch == '\t')
        {
            x += advx * 4.0f;
            continue;
        }
        ui_gl_draw_glyph_grid(b, f, x, y, (uint32_t)ch, t->color, (float)f->cell_w, (float)f->cell_h);
        x += advx;
    }
}

static void ui_gl_draw_icon(ui_gl_backend_t *b, const ui_cmd_icon_t *ic)
{
    ui_gl_font_t *f = ui_gl_get_font(b, ic->font_id);
    if (!f || !f->tex)
        return;

    float sx = (float)f->cell_w;
    float sy = (float)f->cell_h;

    float scale_x = ic->rect.z / (sx > 0.0f ? sx : 1.0f);
    float scale_y = ic->rect.w / (sy > 0.0f ? sy : 1.0f);
    float scale = scale_x < scale_y ? scale_x : scale_y;

    float w = sx * scale;
    float h = sy * scale;

    float px = ic->rect.x + (ic->rect.z - w) * 0.5f;
    float py = ic->rect.y + (ic->rect.w - h) * 0.5f;

    ui_gl_draw_glyph_grid(b, f, px, py, ic->icon_id, ic->color, w, h);
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
}

ui_base_backend_t *ui_gl_backend_base(ui_gl_backend_t *b)
{
    return &b->base;
}

int ui_gl_backend_text_width(void *user, uint32_t font_id, const char *text, int len)
{
    ui_gl_backend_t *b = (ui_gl_backend_t *)user;
    const ui_gl_font_t *f = ui_gl_backend_get_font(b, font_id);
    if (!f || !text)
        return 0;

    float adv = f->advance_x > 0.0f ? f->advance_x : (float)f->cell_w;

    if (len < 0)
    {
        int n = 0;
        while (text[n])
            n++;
        len = n;
    }

    float x = 0.0f;
    float best = 0.0f;

    for (int i = 0; i < len; ++i)
    {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '\n')
        {
            if (x > best)
                best = x;
            x = 0.0f;
            continue;
        }
        if (ch == '\r')
            continue;
        if (ch == '\t')
        {
            x += adv * 4.0f;
            continue;
        }
        x += adv;
    }

    if (x > best)
        best = x;
    return (int)(best + 0.5f);
}

float ui_gl_backend_text_height(void *user, uint32_t font_id)
{
    ui_gl_backend_t *b = (ui_gl_backend_t *)user;
    const ui_gl_font_t *f = ui_gl_backend_get_font(b, font_id);
    if (!f)
        return 0.0f;
    return f->advance_y > 0.0f ? f->advance_y : (float)f->cell_h;
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

int ui_gl_backend_set_font(ui_gl_backend_t *b, uint32_t font_id, ui_gl_font_t font)
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
    *dst = font;
    if (dst->advance_x <= 0.0f)
        dst->advance_x = (float)dst->cell_w;
    if (dst->advance_y <= 0.0f)
        dst->advance_y = (float)dst->cell_h;

    return 1;
}



uint32_t ui_gl_backend_upload_rgba_texture(ui_gl_backend_t *b, int w, int h, const uint8_t *rgba)
{
    (void)b;
    if (w <= 0 || h <= 0 || !rgba) return 0;

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

static int ui_gl_backend_add_font_from_ui_font(ui_gl_backend_t *b, uint32_t font_id, const ui_font_t *f)
{
    if (!b || !f || !f->rgba) return 0;

    uint32_t tex = ui_gl_backend_upload_rgba_texture(b, f->atlas_w, f->atlas_h, f->rgba);
    if (!tex) return 0;

    ui_gl_font_t gf;
    memset(&gf, 0, sizeof(gf));
    gf.tex = tex;
    gf.cell_w = f->cell_w;
    gf.cell_h = f->cell_h;
    gf.cols = f->cols;
    gf.rows = f->rows;
    gf.first_char = f->first_char;
    gf.char_count = f->char_count;
    gf.advance_x = f->advance_x;
    gf.advance_y = f->advance_y;

    return ui_gl_backend_set_font(b, font_id, gf);
}

int ui_gl_backend_add_font_from_ttf_file(ui_gl_backend_t *b, uint32_t font_id, const char *path, float px_height, uint32_t first_char, uint32_t char_count)
{
    if (!b || !path) return 0;

    ui_font_t f;
    if (!ui_font_load_ttf_file(&f, b->rfn, b->ruser, path, px_height, first_char, char_count))
        return 0;

    int ok = ui_gl_backend_add_font_from_ui_font(b, font_id, &f);

    ui_font_free(&f);
    return ok;
}

int ui_gl_backend_add_font_from_ttf_memory(ui_gl_backend_t *b, uint32_t font_id, const void *data, uint32_t size, float px_height, uint32_t first_char, uint32_t char_count)
{
    if (!b || !data || size == 0) return 0;

    ui_font_t f;
    if (!ui_font_load_ttf_memory(&f, b->rfn, b->ruser, data, size, px_height, first_char, char_count))
        return 0;

    int ok = ui_gl_backend_add_font_from_ui_font(b, font_id, &f);

    ui_font_free(&f);
    return ok;
}
