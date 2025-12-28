#pragma once
#include <stdint.h>
#include <GL/glew.h>
#include "../ui_backend.h"
#include "../ui_font.h"

typedef struct ui_gl_font_t
{
    uint32_t tex;
    int cell_w;
    int cell_h;
    int cols;
    int rows;
    uint32_t first_char;
    uint32_t char_count;
    float advance_x;
    float advance_y;
} ui_gl_font_t;

typedef struct ui_gl_backend_t
{
    ui_base_backend_t base;

    ui_realloc_fn rfn;
    void *ruser;

    int fb_w;
    int fb_h;

    GLuint prog;
    GLuint vao;
    GLuint vbo;

    GLint u_screen;
    GLint u_tex;

    GLuint white_tex;

    ui_array_t verts;
    uint32_t cur_tex;

    ui_vec4_t cur_clip;
    int scissor_enabled;

    ui_array_t fonts;
} ui_gl_backend_t;

int ui_gl_backend_init(ui_gl_backend_t *b, ui_realloc_fn rfn, void *ruser);
void ui_gl_backend_shutdown(ui_gl_backend_t *b);

ui_base_backend_t *ui_gl_backend_base(ui_gl_backend_t *b);

int ui_gl_backend_set_font(ui_gl_backend_t *b, uint32_t font_id, ui_gl_font_t font);
const ui_gl_font_t *ui_gl_backend_get_font(const ui_gl_backend_t *b, uint32_t font_id);

int ui_gl_backend_text_width(void *user, uint32_t font_id, const char *text, int len);
float ui_gl_backend_text_height(void *user, uint32_t font_id);

uint32_t ui_gl_backend_upload_rgba_texture(ui_gl_backend_t *b, int w, int h, const uint8_t *rgba);
int ui_gl_backend_add_font_from_ttf_file(ui_gl_backend_t *b, uint32_t font_id, const char *path, float px_height, uint32_t first_char, uint32_t char_count);
int ui_gl_backend_add_font_from_ttf_memory(ui_gl_backend_t *b, uint32_t font_id, const void *data, uint32_t size, float px_height, uint32_t first_char, uint32_t char_count);
