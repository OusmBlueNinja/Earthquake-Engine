#include "core/systems/ecs/entity.h"
#include "core/systems/ecs/internal.h"
#include "core/systems/ecs/pool.h"

ecs_entity_t ecs_world_root(const ecs_world_t *w)
{
    if (!w)
        return 0;
    return w->root_entity;
}

static int ecs_entity_is_alive_indexed(const ecs_world_t *w, uint32_t idx, uint32_t gen)
{
    if (idx >= w->entity_gen.size)
        return 0;
    if (!ecs_vec_u8_get(&w->entity_alive, idx))
        return 0;
    if (ecs_vec_u32_get(&w->entity_gen, idx) != gen)
        return 0;
    return 1;
}

int ecs_entity_is_alive(const ecs_world_t *w, ecs_entity_t e)
{
    if (!w)
        return 0;
    return ecs_entity_is_alive_indexed(w, ecs_entity_index(e), ecs_entity_gen(e));
}

int ecs_entity_is_root(const ecs_world_t *w, ecs_entity_t e)
{
    if (!w)
        return 0;
    return e == w->root_entity;
}

ecs_entity_t ecs_entity_get_parent(const ecs_world_t *w, ecs_entity_t e)
{
    if (!w)
        return 0;
    if (!ecs_entity_is_alive(w, e))
        return 0;
    uint32_t idx = ecs_entity_index(e);
    if (idx >= w->entity_parent.size)
        return 0;
    return ecs_vec_ent_get(&w->entity_parent, idx);
}

static int ecs_entity_would_cycle(ecs_world_t *w, ecs_entity_t child, ecs_entity_t parent)
{
    ecs_entity_t cur = parent;
    while (cur != 0)
    {
        if (cur == child)
            return 1;
        cur = ecs_entity_get_parent(w, cur);
    }
    return 0;
}

int ecs_entity_set_parent(ecs_world_t *w, ecs_entity_t e, ecs_entity_t parent)
{
    if (!w)
        return 0;
    if (!ecs_entity_is_alive(w, e))
        return 0;

    if (e == w->root_entity)
        return 0;

    if (parent == 0)
        return 0;

    if (!ecs_entity_is_alive(w, parent))
        return 0;

    if (ecs_entity_would_cycle(w, e, parent))
        return 0;

    uint32_t idx = ecs_entity_index(e);
    ecs_vec_ent_set(&w->entity_parent, idx, parent);
    return 1;
}

ecs_entity_t ecs_entity_create(ecs_world_t *w)
{
    if (!w)
        return 0;

    uint32_t idx = 0;

    if (w->free_list.size)
    {
        uint32_t *last = vector_back_type(&w->free_list, uint32_t);
        idx = *last;
        vector_pop_back(&w->free_list);

        ecs_vec_u8_set(&w->entity_alive, idx, 1);

        if (!ecs_vec_u32_get(&w->entity_gen, idx))
            ecs_vec_u32_set(&w->entity_gen, idx, 1);
    }
    else
    {
        idx = w->entity_gen.size;
        ecs_world_ensure_entity_capacity_for_index(w, idx + 1);
        ecs_vec_u32_set(&w->entity_gen, idx, 1);
        ecs_vec_u8_set(&w->entity_alive, idx, 1);
    }

    uint32_t gen = ecs_vec_u32_get(&w->entity_gen, idx);
    ecs_entity_t e = ecs_entity_pack(idx, gen);

    ecs_vec_ent_set(&w->entity_parent, idx, w->root_entity);

    if (w->required_tag_id && w->required_tag_id < w->types.size)
        ecs_add_raw(w, e, w->required_tag_id);

    return e;
}

void ecs_entity_destroy(ecs_world_t *w, ecs_entity_t e)
{
    if (!w)
        return;

    if (e == w->root_entity)
        return;

    uint32_t idx = ecs_entity_index(e);
    uint32_t gen = ecs_entity_gen(e);

    if (!ecs_entity_is_alive_indexed(w, idx, gen))
        return;

    ecs_entity_t root = w->root_entity;

    for (uint32_t i = 0; i < w->entity_parent.size; ++i)
    {
        if (!ecs_vec_u8_get(&w->entity_alive, i))
            continue;

        ecs_entity_t p = ecs_vec_ent_get(&w->entity_parent, i);
        if (p == e)
            ecs_vec_ent_set(&w->entity_parent, i, root);
    }

    ecs_component_id_t saved_req = w->required_tag_id;
    w->required_tag_id = 0;

    ecs_pool_t *p = NULL;
    VECTOR_FOR_EACH(w->pools, ecs_pool_t, p)
    {
        ecs_remove_raw(w, e, p->type_id);
    }

    w->required_tag_id = saved_req;

    ecs_vec_u8_set(&w->entity_alive, idx, 0);
    ecs_vec_u32_set(&w->entity_gen, idx, ecs_vec_u32_get(&w->entity_gen, idx) + 1);
    ecs_vec_ent_set(&w->entity_parent, idx, 0);

    vector_push_back(&w->free_list, &idx);
}
