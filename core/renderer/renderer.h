#pragma once

#include "types/vec2.h"
#include "types/vec2i.h"
#include "types/vec4.h"
#include "types/mat4.h"
#include "types/vector.h"
#include "camera.h"
#include "model.h"
#include "light.h"
#include "shader.h"

typedef struct pushed_model_t
{
    const model_t *model;
    mat4 model_matrix;
} pushed_model_t;

typedef struct renderer_t
{
    uint32_t fbo;
    uint32_t color_tex;
    uint32_t depth_tex;
    vec2i fb_size;
    vec4 clear_color;

    camera_t camera;

    vector_t lights;  // vector<light_t>
    vector_t models;  // vector<pushed_model_t>
    vector_t shaders; // vector<shader_t*>

    uint8_t default_shader_id;
} renderer_t;

int R_init(renderer_t *r);
void R_shutdown(renderer_t *r);
void R_resize(renderer_t *r, vec2i size);
void R_set_clear_color(renderer_t *r, vec4 color);
void R_begin_frame(renderer_t *r);
void R_end_frame(renderer_t *r);

void R_push_camera(renderer_t *r, const camera_t *cam);
void R_push_light(renderer_t *r, light_t light);
void R_push_model(renderer_t *r, const model_t *model, mat4 model_matrix);

uint32_t R_get_color_texture(const renderer_t *r);
uint32_t R_get_depth_texture(const renderer_t *r);

uint8_t R_add_shader(renderer_t *r, shader_t *shader); // returns shader index
shader_t *R_get_shader(const renderer_t *r, uint8_t shader_id);
