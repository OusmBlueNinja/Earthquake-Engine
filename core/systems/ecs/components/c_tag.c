#include "c_tag.h"

#include "ecs/component.h"

void c_tag_register(ecs_world_t *w)
{
    ecs_register_component(w, c_tag_t);
}
