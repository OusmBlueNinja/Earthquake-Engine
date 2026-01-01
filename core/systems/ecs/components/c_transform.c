#include "c_transform.h"
#include "ecs/component.h"

static void c_transform_ctor(void *component)
{
    c_transform_t *t = (c_transform_t *)component;
    t->position = (vec3){0.0f, 0.0f, 0.0f};
    t->rotation = (vec3){0.0f, 0.0f, 0.0f};
    t->scale = (vec3){1.0f, 1.0f, 1.0f};
}

void c_transform_register(ecs_world_t *w)
{
    ecs_register_component_ctor(w, c_transform_t, c_transform_ctor);
}
