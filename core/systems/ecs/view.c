#include "core/systems/ecs/view.h"

#include "core/systems/ecs/internal.h"
#include "core/systems/ecs/entity.h"

static ecs_pool_t *ecs_view_pool_at(ecs_world_t *w, uint32_t pool_index)
{
    return (ecs_pool_t *)vector_impl_at(&w->pools, pool_index);
}

static uint32_t ecs_view_sparse_get(const ecs_pool_t *p, uint32_t entity_index)
{
    if (entity_index >= p->sparse.size)
        return ECS_INVALID_U32;
    return ecs_vec_u32_get(&p->sparse, entity_index);
}

static int ecs_view_has_idx(const ecs_pool_t *p, uint32_t idx, uint32_t *out_di)
{
    uint32_t di = ecs_view_sparse_get(p, idx);
    if (di == ECS_INVALID_U32)
        return 0;
    if (di >= p->dense_entities.size)
        return 0;
    uint32_t owner = *(uint32_t *)vector_impl_at(&p->dense_entities, di);
    if (owner != idx)
        return 0;
    if (out_di)
        *out_di = di;
    return 1;
}

static ecs_component_id_t ecs_view_validate_id(const ecs_world_t *w, ecs_component_id_t id)
{
    if (id == 0)
        return 0;
    if (id >= w->types.size)
        return 0;
    return id;
}

bool ecs_view_init(ecs_view_t *v, ecs_world_t *w, uint8_t count, const ecs_component_id_t *ids)
{
    if (!v || !w || !ids)
        return false;
    if (count == 0 || count > ECS_VIEW_MAX)
        return false;

    v->w = w;
    v->count = count;
    v->cursor = 0;
    v->primary = 0;

    for (uint8_t i = 0; i < count; ++i)
    {
        ecs_component_id_t id = ecs_view_validate_id(w, ids[i]);
        if (!id)
            return false;

        v->ids[i] = id;
        v->pool_indices[i] = ECS_INVALID_U32;

        ecs_type_info_t *ti = (ecs_type_info_t *)vector_impl_at(&w->types, id);
        v->comp_sizes[i] = ti ? ti->size : 0;
        if (!v->comp_sizes[i])
            return false;
    }

    uint32_t best = 0xFFFFFFFFu;
    uint8_t primary = 0;

    for (uint8_t i = 0; i < count; ++i)
    {
        uint32_t found = ECS_INVALID_U32;

        for (uint32_t pi = 0; pi < w->pools.size; ++pi)
        {
            ecs_pool_t *p = (ecs_pool_t *)vector_impl_at(&w->pools, pi);
            if (p && p->type_id == v->ids[i])
            {
                found = pi;
                break;
            }
        }

        if (found == ECS_INVALID_U32)
            return false;

        v->pool_indices[i] = found;

        ecs_pool_t *p = ecs_view_pool_at(w, found);
        uint32_t n = p ? p->dense_entities.size : 0;

        if (n < best)
        {
            best = n;
            primary = i;
        }
    }

    v->primary = primary;
    return true;
}

void ecs_view_reset(ecs_view_t *v)
{
    if (!v)
        return;
    v->cursor = 0;
}

bool ecs_view_next(ecs_view_t *v, ecs_entity_t *out_e, void **out_components)
{
    if (!v || !v->w || !out_e || !out_components)
        return false;

    ecs_world_t *w = v->w;

    if (v->primary >= v->count)
        return false;

    uint32_t primary_pool_index = v->pool_indices[v->primary];
    if (primary_pool_index == ECS_INVALID_U32)
        return false;

    ecs_pool_t *pp = ecs_view_pool_at(w, primary_pool_index);
    if (!pp)
        return false;

    uint32_t pn = pp->dense_entities.size;

    for (; v->cursor < pn; ++v->cursor)
    {
        uint32_t idx = *(uint32_t *)vector_impl_at(&pp->dense_entities, v->cursor);

        if (idx >= w->entity_alive.size)
            continue;
        if (!ecs_vec_u8_get(&w->entity_alive, idx))
            continue;

        bool ok = true;

        for (uint8_t i = 0; i < v->count; ++i)
        {
            uint32_t pool_index = v->pool_indices[i];
            ecs_pool_t *p = ecs_view_pool_at(w, pool_index);

            uint32_t di = ECS_INVALID_U32;
            if (!p || !ecs_view_has_idx(p, idx, &di))
            {
                ok = false;
                break;
            }

            out_components[i] = (void *)((uint8_t *)p->dense_data.data + (size_t)di * (size_t)v->comp_sizes[i]);
        }

        if (!ok)
            continue;

        uint32_t gen = ecs_vec_u32_get(&w->entity_gen, idx);
        *out_e = ecs_entity_pack(idx, gen);

        ++v->cursor;
        return true;
    }

    return false;
}
