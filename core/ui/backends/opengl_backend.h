#pragma once
#include <stdint.h>
#include <GL/glew.h>
#include "../ui.h"
#include "../ui_font.h"

typedef struct ui_array_t ui_array_t;

typedef struct ui_gl_font_t
{
    uint32_t tex;
    int tex_w;
    int tex_h;
    float line_h;
    float ascent_px;
    ui_font_t font;
    uint8_t used;
} ui_gl_font_t;

typedef struct ui_gl_backend_t
{
    ui_realloc_fn rfn;
    void *ruser;

    int fb_w;
    int fb_h;

    ui_array_t verts;
    ui_array_t fonts;

    GLuint prog;
    GLint u_screen;
    GLint u_tex;

    GLuint vao;
    GLuint vbo;

    uint32_t white_tex;
    uint32_t cur_tex;

    ui_vec4_t cur_clip;
    int scissor_enabled;

    struct
    {
        GLint program;
        GLint active_texture;
        GLint texture_2d;
        GLint array_buffer;
        GLint element_array_buffer;
        GLint vertex_array;
        GLint viewport[4];
        GLint scissor_box[4];
        GLint blend_src_rgb;
        GLint blend_dst_rgb;
        GLint blend_src_alpha;
        GLint blend_dst_alpha;
        GLint blend_equation_rgb;
        GLint blend_equation_alpha;
        GLint cull_face_mode;
        GLint front_face;
        GLint polygon_mode[2];
        GLint draw_fbo;
        GLint read_fbo;
        GLboolean color_mask[4];
        GLboolean depth_mask;
        GLboolean blend;
        GLboolean depth_test;
        GLboolean cull_face;
        GLboolean scissor_test;
        GLboolean multisample;
        GLboolean sample_alpha_to_coverage;
        GLboolean sample_coverage;
        int valid;
    } saved;

    ui_base_backend_t base;
} ui_gl_backend_t;

int ui_gl_backend_init(ui_gl_backend_t *b, ui_realloc_fn rfn, void *ruser);
void ui_gl_backend_shutdown(ui_gl_backend_t *b);

ui_base_backend_t *ui_gl_backend_base(ui_gl_backend_t *b);

const ui_gl_font_t *ui_gl_backend_get_font(const ui_gl_backend_t *b, uint32_t font_id);

int ui_gl_backend_add_font_from_ttf_file(ui_gl_backend_t *b, uint32_t font_id, const char *path, float px_height);
int ui_gl_backend_add_font_from_ttf_memory(ui_gl_backend_t *b, uint32_t font_id, const void *data, uint32_t size, float px_height);
