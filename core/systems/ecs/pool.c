#include "pool.h"
#include "internal.h"

static ecs_pool_t *ecs_pool_find(ecs_world_t *w, ecs_component_id_t type_id)
{
    ecs_pool_t *p = NULL;
    VECTOR_FOR_EACH(w->pools, ecs_pool_t, p)
    {
        if (p->type_id == type_id)
            return p;
    }
    return NULL;
}

ecs_pool_t *ecs_pool_get_or_create(ecs_world_t *w, ecs_component_id_t type_id)
{
    ecs_pool_t *p = ecs_pool_find(w, type_id);
    if (p)
        return p;

    ecs_pool_t np = (ecs_pool_t){0};
    np.type_id = type_id;
    np.dense_entities = create_vector(uint32_t);
    np.dense_data = create_vector(uint8_t);
    np.sparse = create_vector(uint32_t);

    uint32_t inv = ECS_INVALID_U32;
    vector_resize(&np.sparse, w->entity_gen.size, &inv);

    vector_push_back(&w->pools, &np);
    return vector_back_type(&w->pools, ecs_pool_t);
}

static int ecs_valid_entity_for_world(const ecs_world_t *w, ecs_entity_t e)
{
    uint32_t idx = ecs_entity_index(e);
    if (idx >= w->entity_gen.size)
        return 0;
    if (!ecs_vec_u8_get(&w->entity_alive, idx))
        return 0;
    if (ecs_vec_u32_get(&w->entity_gen, idx) != ecs_entity_gen(e))
        return 0;
    return 1;
}

static uint32_t ecs_pool_sparse_get(const ecs_pool_t *p, uint32_t entity_index)
{
    if (entity_index >= p->sparse.size)
        return ECS_INVALID_U32;
    return ecs_vec_u32_get(&p->sparse, entity_index);
}

static void ecs_pool_sparse_set(ecs_pool_t *p, uint32_t entity_index, uint32_t dense_index)
{
    ecs_vec_u32_set(&p->sparse, entity_index, dense_index);
}

int ecs_has_raw(const ecs_world_t *w, ecs_entity_t e, ecs_component_id_t type_id)
{
    if (!w)
        return 0;
    if (!ecs_valid_entity_for_world(w, e))
        return 0;
    if (type_id == 0 || type_id >= w->types.size)
        return 0;

    ecs_pool_t *p = ecs_pool_find((ecs_world_t *)w, type_id);
    if (!p)
        return 0;

    uint32_t idx = ecs_entity_index(e);
    uint32_t di = ecs_pool_sparse_get(p, idx);
    if (di == ECS_INVALID_U32)
        return 0;
    if (di >= p->dense_entities.size)
        return 0;

    uint32_t owner = *(uint32_t *)vector_impl_at(&p->dense_entities, di);
    return owner == idx;
}

void *ecs_get_raw(ecs_world_t *w, ecs_entity_t e, ecs_component_id_t type_id)
{
    if (!ecs_has_raw(w, e, type_id))
        return NULL;

    ecs_pool_t *p = ecs_pool_find(w, type_id);
    uint32_t di = ecs_pool_sparse_get(p, ecs_entity_index(e));

    ecs_type_info_t *ti = (ecs_type_info_t *)vector_impl_at(&w->types, type_id);
    return (void *)((uint8_t *)p->dense_data.data + (size_t)di * ti->size);
}

void *ecs_add_raw(ecs_world_t *w, ecs_entity_t e, ecs_component_id_t type_id)
{
    if (!w)
        return NULL;
    if (!ecs_valid_entity_for_world(w, e))
        return NULL;
    if (type_id == 0 || type_id >= w->types.size)
        return NULL;

    if (w->required_tag_id && w->required_tag_id < w->types.size && type_id != w->required_tag_id)
    {
        if (!ecs_has_raw(w, e, w->required_tag_id))
            ecs_add_raw(w, e, w->required_tag_id);
    }

    ecs_pool_t *p = ecs_pool_get_or_create(w, type_id);
    uint32_t idx = ecs_entity_index(e);

    if (idx >= p->sparse.size)
    {
        uint32_t inv = ECS_INVALID_U32;
        vector_resize(&p->sparse, idx + 1, &inv);
    }

    uint32_t di = ecs_pool_sparse_get(p, idx);
    if (di != ECS_INVALID_U32)
        return ecs_get_raw(w, e, type_id);

    ecs_type_info_t *ti = (ecs_type_info_t *)vector_impl_at(&w->types, type_id);

    uint32_t dense_index = p->dense_entities.size;
    vector_push_back(&p->dense_entities, &idx);

    uint8_t z = 0;
    vector_resize(&p->dense_data, (dense_index + 1) * ti->size, &z);

    ecs_pool_sparse_set(p, idx, dense_index);

    uint8_t *ptr = (uint8_t *)p->dense_data.data + (size_t)dense_index * ti->size;
    base_component_t *b = (base_component_t *)(ptr + ti->base_offset);
    b->entity = e;
    b->type_id = type_id;
    b->flags = 0;
    b->save_fn = ti->save_fn;
    b->load_fn = ti->load_fn;

    return (void *)ptr;
}

int ecs_remove_raw(ecs_world_t *w, ecs_entity_t e, ecs_component_id_t type_id)
{
    if (!w)
        return 0;
    if (!ecs_valid_entity_for_world(w, e))
        return 0;
    if (type_id == 0 || type_id >= w->types.size)
        return 0;

    if (w->required_tag_id && type_id == w->required_tag_id)
        return 0;

    ecs_pool_t *p = ecs_pool_find(w, type_id);
    if (!p)
        return 0;

    uint32_t idx = ecs_entity_index(e);
    uint32_t di = ecs_pool_sparse_get(p, idx);
    if (di == ECS_INVALID_U32)
        return 0;
    if (di >= p->dense_entities.size)
        return 0;

    uint32_t owner = *(uint32_t *)vector_impl_at(&p->dense_entities, di);
    if (owner != idx)
        return 0;

    ecs_type_info_t *ti = (ecs_type_info_t *)vector_impl_at(&w->types, type_id);

    uint32_t last = p->dense_entities.size - 1;
    if (di != last)
    {
        uint32_t moved_entity = *(uint32_t *)vector_impl_at(&p->dense_entities, last);
        *(uint32_t *)vector_impl_at(&p->dense_entities, di) = moved_entity;

        uint8_t *dst = (uint8_t *)p->dense_data.data + (size_t)di * ti->size;
        uint8_t *src = (uint8_t *)p->dense_data.data + (size_t)last * ti->size;
        memcpy(dst, src, ti->size);

        ecs_pool_sparse_set(p, moved_entity, di);
    }

    vector_pop_back(&p->dense_entities);

    uint8_t z = 0;
    vector_resize(&p->dense_data, p->dense_entities.size * ti->size, &z);

    ecs_pool_sparse_set(p, idx, ECS_INVALID_U32);
    return 1;
}

uint32_t ecs_count_raw(const ecs_world_t *w, ecs_component_id_t type_id)
{
    if (!w)
        return 0;
    if (type_id == 0 || type_id >= w->types.size)
        return 0;

    ecs_pool_t *p = ecs_pool_find((ecs_world_t *)w, type_id);
    if (!p)
        return 0;

    return p->dense_entities.size;
}

void *ecs_dense_raw(ecs_world_t *w, ecs_component_id_t type_id)
{
    if (!w)
        return NULL;
    if (type_id == 0 || type_id >= w->types.size)
        return NULL;

    ecs_pool_t *p = ecs_pool_find(w, type_id);
    if (!p)
        return NULL;

    return p->dense_data.data;
}

ecs_entity_t ecs_entity_at_raw(const ecs_world_t *w, ecs_component_id_t type_id, uint32_t dense_index)
{
    if (!w)
        return 0;
    if (type_id == 0 || type_id >= w->types.size)
        return 0;

    ecs_pool_t *p = ecs_pool_find((ecs_world_t *)w, type_id);
    if (!p)
        return 0;
    if (dense_index >= p->dense_entities.size)
        return 0;

    uint32_t idx = *(uint32_t *)vector_impl_at(&p->dense_entities, dense_index);
    uint32_t gen = ecs_vec_u32_get(&w->entity_gen, idx);
    return ecs_entity_pack(idx, gen);
}
