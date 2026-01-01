#include "entity.h"
#include "internal.h"
#include "pool.h"

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

    if (w->required_tag_id && w->required_tag_id < w->types.size)
        ecs_add_raw(w, e, w->required_tag_id);

    return e;
}

void ecs_entity_destroy(ecs_world_t *w, ecs_entity_t e)
{
    if (!w)
        return;

    uint32_t idx = ecs_entity_index(e);
    uint32_t gen = ecs_entity_gen(e);

    if (!ecs_entity_is_alive_indexed(w, idx, gen))
        return;

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

    vector_push_back(&w->free_list, &idx);
}
