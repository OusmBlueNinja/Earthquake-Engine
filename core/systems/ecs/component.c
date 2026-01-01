#include "component.h"
#include "internal.h"

static const ecs_type_info_t *ecs_type_at_const(const ecs_world_t *w, ecs_component_id_t id)
{
    return (const ecs_type_info_t *)((uint8_t *)w->types.data + (size_t)id * w->types.element_size);
}

ecs_component_id_t ecs_component_id_by_name(const ecs_world_t *w, const char *name)
{
    if (!w || !name || !name[0])
        return 0;

    for (uint32_t i = 1; i < w->types.size; ++i)
    {
        const ecs_type_info_t *t = ecs_type_at_const(w, (ecs_component_id_t)i);
        if (t->name && strcmp(t->name, name) == 0)
            return (ecs_component_id_t)i;
    }

    return 0;
}

ecs_component_id_t ecs_register_component_type(
    ecs_world_t *w,
    const char *name,
    uint32_t size,
    uint32_t base_offset,
    ecs_component_save_fn save_fn,
    ecs_component_load_fn load_fn,
    ecs_component_ctor_fn ctor_fn
)
{
    if (!w || !name || !name[0] || !size)
        return 0;

    ecs_component_id_t existing = ecs_component_id_by_name(w, name);
    if (existing)
        return existing;

    ecs_type_info_t t = (ecs_type_info_t){0};
    t.name = ecs_strdup_local(name);
    t.size = size;
    t.base_offset = base_offset;
    t.save_fn = save_fn;
    t.load_fn = load_fn;
    t.ctor_fn = ctor_fn;

    vector_push_back(&w->types, &t);

    ecs_component_id_t id = (ecs_component_id_t)(w->types.size - 1);
    ecs_pool_get_or_create(w, id);

    return id;
}
