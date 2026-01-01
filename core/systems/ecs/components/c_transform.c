#include "c_transform.h"
#include "ecs/component.h"

void c_transform_register(ecs_world_t *w)
{
    ecs_register_component(w, c_transform_t);
}
