#pragma once
#include <stdint.h>

typedef struct renderer_t renderer_t;

typedef struct bloom_t
{
    uint8_t ready;

    int last_w;
    int last_h;
    uint32_t mips;

    uint32_t tex_down[16];
    uint32_t tex_up[16];

    uint32_t fbo_down[16];
    uint32_t fbo_up[16];

    uint32_t prefilter_shader_id;
    uint32_t downsample_shader_id;
    uint32_t blur_h_shader_id;
    uint32_t blur_v_shader_id;
    uint32_t upsample_shader_id;
    uint32_t composite_shader_id;
} bloom_t;

int bloom_init(renderer_t *r);
void bloom_shutdown(renderer_t *r);
void bloom_ensure(renderer_t *r);
void bloom_run(renderer_t *r, uint32_t src_tex, uint32_t black_tex);
uint32_t bloom_get_texture(renderer_t *r);
void bloom_composite_to_final(renderer_t *r, uint32_t scene_tex, uint32_t bloom_tex, uint32_t depth_tex, uint32_t black_tex);
