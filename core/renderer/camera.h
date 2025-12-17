#pragma once
#include "types/vec3.h"
#include "types/mat4.h"

typedef struct camera_t
{
    mat4 view;
    mat4 proj;
    vec3 position;
} camera_t;

static inline camera_t camera_create(void)
{
    camera_t c;
    c.view = mat4_identity();
    c.proj = mat4_identity();
    c.position = (vec3){0, 0, 0};
    return c;
}

static inline void camera_set_perspective(camera_t *c,
                                          float fov_radians,
                                          float aspect,
                                          float near_plane,
                                          float far_plane)
{
    c->proj = mat4_perspective(fov_radians, aspect, near_plane, far_plane);
}

static inline void camera_set_orthographic(camera_t *c,
                                           float left,
                                           float right,
                                           float bottom,
                                           float top,
                                           float near_plane,
                                           float far_plane)
{
    c->proj = mat4_ortho(left, right, bottom, top, near_plane, far_plane);
}

static inline void camera_look_at(camera_t *c,
                                  vec3 position,
                                  vec3 target,
                                  vec3 up)
{
    c->position = position;
    c->view = mat4_lookat(position, target, up);
}

static inline void camera_move(camera_t *c, vec3 delta)
{
    c->position.x += delta.x;
    c->position.y += delta.y;
    c->position.z += delta.z;
}

static inline const mat4 *camera_view(const camera_t *c)
{
    return &c->view;
}

static inline const mat4 *camera_proj(const camera_t *c)
{
    return &c->proj;
}

static inline mat4 camera_view_proj(const camera_t *c)
{
    return mat4_mul(c->proj, c->view);
}
