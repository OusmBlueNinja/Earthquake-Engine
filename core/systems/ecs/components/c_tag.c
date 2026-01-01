#include "c_tag.h"

#include "ecs/component.h"

static void c_tag_ctor(void *component)
{
    c_tag_t *t = (c_tag_t *)component;
    memcpy(t->name, "Entity", sizeof("Entity"));
    t->layer = 1;
    t->visible = 1;
}

void c_tag_register(ecs_world_t *w)
{
    ecs_register_component_ctor(w, c_tag_t, c_tag_ctor);
}
