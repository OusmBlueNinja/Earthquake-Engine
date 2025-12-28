#include "ui_font.h"
#include <string.h>
#include <stdio.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

static void *uifr(ui_font_t *f, void *p, uint32_t sz)
{
    return f->rfn(f->ruser, p, sz);
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

static int ui_font_build(ui_font_t *f, const uint8_t *ttf, uint32_t ttf_size, float px_height, uint32_t first_char, uint32_t char_count)
{
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf, 0))
        return 0;

    float scale = stbtt_ScaleForPixelHeight(&info, px_height);

    int ascent = 0, descent = 0, linegap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &linegap);

    float advy = (float)(ascent - descent + linegap) * scale;
    if (advy < px_height)
        advy = px_height;

    int max_aw = 0;
    for (uint32_t c = 0; c < char_count; ++c)
    {
        int aw = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, (int)(first_char + c), &aw, &lsb);
        if (aw > max_aw)
            max_aw = aw;
    }

    float advx = (float)max_aw * scale;
    if (advx < px_height * 0.5f)
        advx = px_height * 0.5f;

    int pad = 2;
    int cell_w = (int)(advx + 0.5f) + pad * 2;
    int cell_h = (int)(advy + 0.5f) + pad * 2;

    int cols = 1;
    while ((uint32_t)(cols * cols) < char_count)
        cols++;

    int rows = (int)((char_count + (uint32_t)cols - 1) / (uint32_t)cols);

    int atlas_w = cols * cell_w;
    int atlas_h = rows * cell_h;

    uint32_t rgba_size = (uint32_t)(atlas_w * atlas_h * 4);
    uint8_t *rgba = (uint8_t *)uifr(f, 0, rgba_size);
    if (!rgba)
        return 0;
    memset(rgba, 0, rgba_size);

    int baseline = (int)((float)ascent * scale + 0.5f);

    for (uint32_t i = 0; i < char_count; ++i)
    {
        int cp = (int)(first_char + i);

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&info, cp, scale, scale, &x0, &y0, &x1, &y1);

        int gw = x1 - x0;
        int gh = y1 - y0;

        if (gw <= 0 || gh <= 0)
            continue;

        uint8_t *mono = (uint8_t *)uifr(f, 0, (uint32_t)(gw * gh));
        if (!mono)
        {
            uifr(f, rgba, 0);
            return 0;
        }
        memset(mono, 0, (uint32_t)(gw * gh));

        stbtt_MakeCodepointBitmap(&info, mono, gw, gh, gw, scale, scale, cp);

        int col = (int)(i % (uint32_t)cols);
        int row = (int)(i / (uint32_t)cols);

        int cell_x = col * cell_w;
        int cell_y = row * cell_h;

        int dst_x0 = cell_x + pad + x0;
        int dst_y0 = cell_y + pad + (baseline + y0);

        for (int y = 0; y < gh; ++y)
        {
            int dy = dst_y0 + y;
            if (dy < 0 || dy >= atlas_h)
                continue;

            for (int x = 0; x < gw; ++x)
            {
                int dx = dst_x0 + x;
                if (dx < 0 || dx >= atlas_w)
                    continue;

                uint8_t a = mono[y * gw + x];
                uint32_t o = (uint32_t)((dy * atlas_w + dx) * 4);

                rgba[o + 0] = 255;
                rgba[o + 1] = 255;
                rgba[o + 2] = 255;
                rgba[o + 3] = a;
            }
        }

        uifr(f, mono, 0);
    }

    f->rgba = rgba;
    f->rgba_size = rgba_size;
    f->atlas_w = atlas_w;
    f->atlas_h = atlas_h;
    f->cell_w = cell_w;
    f->cell_h = cell_h;
    f->cols = cols;
    f->rows = rows;
    f->first_char = first_char;
    f->char_count = char_count;
    f->advance_x = (float)(cell_w - pad * 2);
    f->advance_y = (float)(cell_h - pad * 2);

    return 1;
}

int ui_font_load_ttf_file(ui_font_t *f, ui_realloc_fn rfn, void *ruser, const char *path, float px_height, uint32_t first_char, uint32_t char_count)
{
    memset(f, 0, sizeof(*f));
    f->rfn = rfn;
    f->ruser = ruser;

    uint8_t *ttf = 0;
    uint32_t ttf_size = 0;
    if (!ui_read_file_all(f, path, &ttf, &ttf_size))
        return 0;

    int ok = ui_font_build(f, ttf, ttf_size, px_height, first_char, char_count);

    uifr(f, ttf, 0);

    return ok;
}

int ui_font_load_ttf_memory(ui_font_t *f, ui_realloc_fn rfn, void *ruser, const void *data, uint32_t size, float px_height, uint32_t first_char, uint32_t char_count)
{
    memset(f, 0, sizeof(*f));
    f->rfn = rfn;
    f->ruser = ruser;

    if (!data || size == 0)
        return 0;

    return ui_font_build(f, (const uint8_t *)data, size, px_height, first_char, char_count);
}

void ui_font_free(ui_font_t *f)
{
    if (!f)
        return;
    if (f->rgba)
        uifr(f, f->rgba, 0);
    memset(f, 0, sizeof(*f));
}
