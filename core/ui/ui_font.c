#include "ui_font.h"
#include <string.h>
#include <stdio.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

typedef struct stbwrap_t
{
    stbtt_fontinfo info;
} stbwrap_t;

static void *uifr(ui_font_t *f, void *p, uint32_t sz)
{
    return f->rfn(f->ruser, p, sz);
}

static uint32_t ui_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static uint32_t ui_utf8_next(const char **p)
{
    const uint8_t *s = (const uint8_t *)(*p);
    if (!s || !*s)
        return 0;

    uint8_t b0 = s[0];

    if (b0 < 0x80)
    {
        *p += 1;
        return (uint32_t)b0;
    }

    if ((b0 & 0xE0) == 0xC0)
    {
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

static int ui_read_file_all(ui_font_t *f, const char *path, uint8_t **out_data, uint32_t *out_size)
{
    *out_data = 0;
    *out_size = 0;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return 0;

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (len <= 0)
    {
        fclose(fp);
        return 0;
    }

    uint8_t *buf = (uint8_t *)uifr(f, 0, (uint32_t)len);
    if (!buf)
    {
        fclose(fp);
        return 0;
    }

    size_t got = fread(buf, 1, (size_t)len, fp);
    fclose(fp);

    if ((long)got != len)
    {
        uifr(f, buf, 0);
        return 0;
    }

    *out_data = buf;
    *out_size = (uint32_t)len;
    return 1;
}

static int ui_atlas_init(ui_font_t *f, int w, int h)
{
    f->atlas_w = w;
    f->atlas_h = h;
    f->pen_x = 2;
    f->pen_y = 2;
    f->row_h = 0;

    uint32_t rgba_size = (uint32_t)(w * h * 4);
    uint8_t *rgba = (uint8_t *)uifr(f, 0, rgba_size);
    if (!rgba)
        return 0;
    memset(rgba, 0, rgba_size);

    f->rgba = rgba;
    f->rgba_size = rgba_size;
    f->dirty = 1;
    return 1;
}

static int ui_glyph_table_init(ui_font_t *f, uint32_t cap_pow2)
{
    f->glyph_cap = cap_pow2;
    f->glyph_count = 0;

    ui_glyph_t *g = (ui_glyph_t *)uifr(f, 0, (uint32_t)(sizeof(ui_glyph_t) * cap_pow2));
    if (!g)
        return 0;
    memset(g, 0, sizeof(ui_glyph_t) * cap_pow2);
    f->glyphs = g;
    return 1;
}

static ui_glyph_t *ui_glyph_table_find_slot(ui_font_t *f, uint32_t cp)
{
    uint32_t mask = f->glyph_cap - 1;
    uint32_t idx = ui_hash_u32(cp) & mask;

    for (;;)
    {
        ui_glyph_t *e = &f->glyphs[idx];
        if (!e->used || e->cp == cp)
            return e;
        idx = (idx + 1) & mask;
    }
}

static int ui_glyph_table_grow(ui_font_t *f)
{
    uint32_t old_cap = f->glyph_cap;
    ui_glyph_t *old = f->glyphs;

    uint32_t new_cap = old_cap ? (old_cap * 2) : 1024;
    ui_glyph_t *ng = (ui_glyph_t *)uifr(f, 0, (uint32_t)(sizeof(ui_glyph_t) * new_cap));
    if (!ng)
        return 0;
    memset(ng, 0, sizeof(ui_glyph_t) * new_cap);

    f->glyphs = ng;
    f->glyph_cap = new_cap;
    f->glyph_count = 0;

    if (old)
    {
        for (uint32_t i = 0; i < old_cap; ++i)
        {
            if (!old[i].used)
                continue;
            ui_glyph_t *slot = ui_glyph_table_find_slot(f, old[i].cp);
            *slot = old[i];
            f->glyph_count++;
        }
        uifr(f, old, 0);
    }

    return 1;
}

static int ui_atlas_grow(ui_font_t *f)
{
    int old_w = f->atlas_w;
    int old_h = f->atlas_h;

    int new_w = old_w * 2;
    int new_h = old_h * 2;

    if (new_w > 4096)
        new_w = 4096;
    if (new_h > 4096)
        new_h = 4096;

    if (new_w == old_w && new_h == old_h)
        return 0;

    uint32_t new_size = (uint32_t)(new_w * new_h * 4);
    uint8_t *nr = (uint8_t *)uifr(f, 0, new_size);
    if (!nr)
        return 0;
    memset(nr, 0, new_size);

    for (int y = 0; y < old_h; ++y)
    {
        memcpy(nr + (uint32_t)(y * new_w * 4), f->rgba + (uint32_t)(y * old_w * 4), (size_t)(old_w * 4));
    }

    uifr(f, f->rgba, 0);
    f->rgba = nr;
    f->rgba_size = new_size;
    f->atlas_w = new_w;
    f->atlas_h = new_h;
    f->dirty = 1;
    return 1;
}

static int ui_atlas_alloc_rect(ui_font_t *f, int w, int h, int *out_x, int *out_y)
{
    int pad = 1;
    w += pad * 2;
    h += pad * 2;

    if (w > f->atlas_w - 4 || h > f->atlas_h - 4)
        return 0;

    if (f->pen_x + w > f->atlas_w - 2)
    {
        f->pen_x = 2;
        f->pen_y += f->row_h;
        f->row_h = 0;
    }

    if (f->pen_y + h > f->atlas_h - 2)
        return 0;

    int x = f->pen_x + pad;
    int y = f->pen_y + pad;

    f->pen_x += w;
    if (h > f->row_h)
        f->row_h = h;

    *out_x = x;
    *out_y = y;
    return 1;
}

static int ui_font_init_from_ttf(ui_font_t *f, const uint8_t *ttf, uint32_t ttf_size, float px_height)
{
    (void)ttf_size;

    stbwrap_t *w = (stbwrap_t *)uifr(f, 0, (uint32_t)sizeof(stbwrap_t));
    if (!w)
        return 0;
    memset(w, 0, sizeof(*w));

    if (!stbtt_InitFont(&w->info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0)))
    {
        uifr(f, w, 0);
        return 0;
    }

    f->stb_info = w;
    f->px_height = px_height;
    f->scale = stbtt_ScaleForPixelHeight(&w->info, px_height);

    stbtt_GetFontVMetrics(&w->info, &f->ascent, &f->descent, &f->linegap);

    if (!ui_atlas_init(f, 1024, 1024))
        return 0;

    if (!ui_glyph_table_init(f, 1024))
        return 0;

    return 1;
}

static int ui_font_bake_codepoint(ui_font_t *f, uint32_t cp)
{
    stbwrap_t *w = (stbwrap_t *)f->stb_info;
    if (!w)
        return 0;

    int glyph = stbtt_FindGlyphIndex(&w->info, (int)cp);
    if (glyph == 0 && cp != 0xFFFD)
        cp = 0xFFFD;

    int aw = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&w->info, (int)cp, &aw, &lsb);

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetCodepointBitmapBox(&w->info, (int)cp, f->scale, f->scale, &x0, &y0, &x1, &y1);

    int gw = x1 - x0;
    int gh = y1 - y0;

    if (gw < 0)
        gw = 0;
    if (gh < 0)
        gh = 0;

    int ax = 0, ay = 0;
    if (!ui_atlas_alloc_rect(f, gw ? gw : 1, gh ? gh : 1, &ax, &ay))
    {
        if (!ui_atlas_grow(f))
            return 0;
        if (!ui_atlas_alloc_rect(f, gw ? gw : 1, gh ? gh : 1, &ax, &ay))
            return 0;
    }

    if (gw > 0 && gh > 0)
    {
        uint8_t *mono = (uint8_t *)uifr(f, 0, (uint32_t)(gw * gh));
        if (!mono)
            return 0;

        stbtt_MakeCodepointBitmap(&w->info, mono, gw, gh, gw, f->scale, f->scale, (int)cp);

        for (int y = 0; y < gh; ++y)
        {
            int dy = ay + y;
            uint32_t row = (uint32_t)(dy * f->atlas_w * 4);
            for (int x = 0; x < gw; ++x)
            {
                int dx = ax + x;
                uint32_t o = row + (uint32_t)(dx * 4);
                uint8_t a = mono[y * gw + x];
                f->rgba[o + 0] = 255;
                f->rgba[o + 1] = 255;
                f->rgba[o + 2] = 255;
                f->rgba[o + 3] = a;
            }
        }

        uifr(f, mono, 0);
    }

    if (f->glyph_count * 10 >= f->glyph_cap * 7)
    {
        if (!ui_glyph_table_grow(f))
            return 0;
    }

    ui_glyph_t *slot = ui_glyph_table_find_slot(f, cp);
    if (!slot->used)
        f->glyph_count++;

    slot->used = 1;
    slot->cp = cp;
    slot->w = gw;
    slot->h = gh;
    slot->xoff = x0;
    slot->yoff = y0;
    slot->xadvance = (float)aw * f->scale;

    slot->u0 = (float)ax / (float)f->atlas_w;
    slot->v0 = (float)ay / (float)f->atlas_h;
    slot->u1 = (float)(ax + (gw ? gw : 1)) / (float)f->atlas_w;
    slot->v1 = (float)(ay + (gh ? gh : 1)) / (float)f->atlas_h;

    f->dirty = 1;
    return 1;
}

const ui_glyph_t *ui_font_get_glyph(ui_font_t *f, uint32_t codepoint)
{
    if (!f || !f->stb_info || !f->glyphs || f->glyph_cap == 0)
        return 0;

    ui_glyph_t *slot = ui_glyph_table_find_slot(f, codepoint);
    if (slot->used && slot->cp == codepoint)
        return slot;

    if (!ui_font_bake_codepoint(f, codepoint))
    {
        if (codepoint != 0xFFFD)
            return ui_font_get_glyph(f, 0xFFFD);
        return 0;
    }

    slot = ui_glyph_table_find_slot(f, codepoint);
    if (slot->used && slot->cp == codepoint)
        return slot;

    if (codepoint != 0xFFFD)
        return ui_font_get_glyph(f, 0xFFFD);

    return 0;
}

void ui_font_draw_prepare_utf8(ui_font_t *f, const char *text)
{
    if (!f || !text)
        return;

    const char *p = text;
    for (;;)
    {
        uint32_t cp = ui_utf8_next(&p);
        if (!cp)
            break;
        ui_font_get_glyph(f, cp);
    }
}

void ui_font_measure_utf8(ui_font_t *f, const char *text, float *out_w, float *out_h)
{
    if (out_w)
        *out_w = 0.0f;
    if (out_h)
        *out_h = 0.0f;

    if (!f || !text)
        return;

    float x = 0.0f;
    float maxx = 0.0f;

    float line_h = (float)(f->ascent - f->descent + f->linegap) * f->scale;
    if (line_h < f->px_height)
        line_h = f->px_height;

    float y = line_h;

    const char *p = text;
    for (;;)
    {
        uint32_t cp = ui_utf8_next(&p);
        if (!cp)
            break;

        if (cp == '\n')
        {
            if (x > maxx)
                maxx = x;
            x = 0.0f;
            y += line_h;
            continue;
        }

        const ui_glyph_t *g = ui_font_get_glyph(f, cp);
        if (g)
            x += g->xadvance;
    }

    if (x > maxx)
        maxx = x;

    if (out_w)
        *out_w = maxx;
    if (out_h)
        *out_h = y;
}

int ui_font_load_ttf_file(ui_font_t *f, ui_realloc_fn rfn, void *ruser, const char *path, float px_height)
{
    memset(f, 0, sizeof(*f));
    f->rfn = rfn;
    f->ruser = ruser;

    uint8_t *ttf = 0;
    uint32_t ttf_size = 0;
    if (!ui_read_file_all(f, path, &ttf, &ttf_size))
        return 0;

    f->ttf = ttf;
    f->ttf_size = ttf_size;

    if (!ui_font_init_from_ttf(f, f->ttf, f->ttf_size, px_height))
    {
        uifr(f, f->ttf, 0);
        memset(f, 0, sizeof(*f));
        return 0;
    }

    ui_font_get_glyph(f, 0xFFFD);
    ui_font_draw_prepare_utf8(f, " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~");

    return 1;
}

int ui_font_load_ttf_memory(ui_font_t *f, ui_realloc_fn rfn, void *ruser, const void *data, uint32_t size, float px_height)
{
    memset(f, 0, sizeof(*f));
    f->rfn = rfn;
    f->ruser = ruser;

    if (!data || size == 0)
        return 0;

    uint8_t *ttf = (uint8_t *)uifr(f, 0, size);
    if (!ttf)
        return 0;
    memcpy(ttf, data, size);

    f->ttf = ttf;
    f->ttf_size = size;

    if (!ui_font_init_from_ttf(f, f->ttf, f->ttf_size, px_height))
    {
        uifr(f, f->ttf, 0);
        memset(f, 0, sizeof(*f));
        return 0;
    }

    ui_font_get_glyph(f, 0xFFFD);
    return 1;
}

static void ui_font_preload_range(ui_font_t *f, uint32_t a, uint32_t b)
{
    for (uint32_t cp = a; cp <= b; ++cp)
        ui_font_get_glyph(f, cp);
}

void ui_font_preload_common(ui_font_t *f)
{
    if (!f)
        return;

    ui_font_preload_range(f, 0x0020, 0x007E);
    ui_font_preload_range(f, 0x00A0, 0x00FF);
    ui_font_preload_range(f, 0x0100, 0x017F);
    ui_font_preload_range(f, 0x0180, 0x024F);
    ui_font_preload_range(f, 0x0370, 0x03FF);
    ui_font_preload_range(f, 0x0400, 0x04FF);
    ui_font_preload_range(f, 0x2000, 0x206F);
    ui_font_preload_range(f, 0x20A0, 0x20CF);
    ui_font_preload_range(f, 0x2190, 0x21FF);
    ui_font_preload_range(f, 0x2200, 0x22FF);
    ui_font_preload_range(f, 0x2500, 0x257F);
    ui_font_preload_range(f, 0x2580, 0x259F);
    ui_font_preload_range(f, 0x25A0, 0x25FF);
}

void ui_font_free(ui_font_t *f)
{
    if (!f)
        return;

    if (f->glyphs)
        uifr(f, f->glyphs, 0);

    if (f->rgba)
        uifr(f, f->rgba, 0);

    if (f->stb_info)
        uifr(f, f->stb_info, 0);

    if (f->ttf)
        uifr(f, f->ttf, 0);

    memset(f, 0, sizeof(*f));
}
