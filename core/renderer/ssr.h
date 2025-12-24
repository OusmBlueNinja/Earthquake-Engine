#pragma once
#include <stdint.h>

typedef struct renderer_t renderer_t;

typedef struct ssr_t
{
    uint32_t fbo;
    uint32_t color_tex;
    uint32_t shader_id;
    int ready;
} ssr_t;

int ssr_init(renderer_t *r);
void ssr_shutdown(renderer_t *r);
void ssr_on_resize(renderer_t *r);
void ssr_ensure(renderer_t *r);
void ssr_run(renderer_t *r, uint32_t scene_tex);
