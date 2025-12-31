#pragma once
#include <stdint.h>

typedef struct gl_state_cache_t
{
    uint8_t cap_known;
    uint8_t cap_blend;
    uint8_t cap_depth_test;
    uint8_t cap_cull_face;
    uint8_t cap_scissor_test;
    uint8_t cap_multisample;
    uint8_t cap_sample_alpha_to_coverage;
    uint8_t cap_polygon_offset_fill;
    uint8_t cap_cube_map_seamless;

    uint8_t blend_func_known;
    uint32_t blend_src_rgb;
    uint32_t blend_dst_rgb;
    uint32_t blend_src_a;
    uint32_t blend_dst_a;

    uint8_t depth_mask_known;
    uint8_t depth_mask;

    uint8_t depth_func_known;
    uint32_t depth_func;
} gl_state_cache_t;

void gl_state_reset(gl_state_cache_t *st);

void gl_state_enable(gl_state_cache_t *st, uint32_t cap);
void gl_state_disable(gl_state_cache_t *st, uint32_t cap);

void gl_state_blend_func(gl_state_cache_t *st, uint32_t src_rgb, uint32_t dst_rgb);
void gl_state_blend_func_separate(gl_state_cache_t *st, uint32_t src_rgb, uint32_t dst_rgb, uint32_t src_a, uint32_t dst_a);

void gl_state_depth_mask(gl_state_cache_t *st, uint8_t enabled);
void gl_state_depth_func(gl_state_cache_t *st, uint32_t func);

