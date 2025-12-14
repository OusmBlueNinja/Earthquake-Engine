#pragma ince
#include <stdint.h>
#include "types/vec4.h"
#include "types/vec2.h"
#include "types/vec2i.h"

typedef struct renderer_t
{
    vec4 clear_color;
    vec2i fb_size;
} renderer_t;

int R_init(renderer_t *r);
void R_shutdown(renderer_t *r);

void R_begin_frame(renderer_t *r, vec2i fb_size);
void R_end_frame(renderer_t *r);

void R_set_clear_color(renderer_t *r, vec4 color);