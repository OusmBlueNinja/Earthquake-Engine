#pragma once
#include <stdint.h>
#include "types/vec2.h"
#include "types/vec2i.h"
#include "types/vec4.h"
#include "types/mat4.h"
#include "camera.h"
#include "light.h"
#include "model.h"

typedef struct renderer_t
{
    vec4 clear_color;
    vec2i fb_size;

    uint32_t fbo;
    uint32_t color_tex;
    uint32_t depth_tex;
} renderer_t;

int R_init(renderer_t *r);
void R_shutdown(renderer_t *r);
void R_resize(renderer_t *r, vec2i size);
void R_begin_frame(renderer_t *r);
void R_end_frame(renderer_t *r);
void R_set_clear_color(renderer_t *r, vec4 color);
void R_push_camera(const camera_t *cam);
void R_push_light(const light_t *light);
void R_draw_model(const model_t *model, mat4 model_matrix);
uint32_t R_get_color_texture(const renderer_t *r);
uint32_t R_get_depth_texture(const renderer_t *r);
