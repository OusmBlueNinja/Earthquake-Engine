#include "core/systems/ecs/components/c_light.h"

#include <string.h>

#include "core/systems/ecs/component.h"

static void c_light_ctor(void *component)
{
    c_light_t *l = (c_light_t *)component;
    l->type = LIGHT_POINT;
    l->color = (vec3){1.0f, 1.0f, 1.0f};
    l->intensity = 1.0f;
    l->radius = 10.0f;
    l->range = 10.0f;
}

void c_light_register(ecs_world_t *w)
{
    ecs_register_component_ctor(w, c_light_t, c_light_ctor);
}
