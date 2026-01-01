#include "core/systems/ecs/world.h"
#include "core/systems/ecs/internal.h"

void ecs_world_init(ecs_world_t *w, ecs_world_desc_t desc)
{
    if (!w)
        return;

    *w = (ecs_world_t){0};

    w->entity_gen = create_vector(uint32_t);
    w->entity_alive = create_vector(uint8_t);
    w->entity_parent = create_vector(ecs_entity_t);
    w->free_list = create_vector(uint32_t);

    w->types = create_vector(ecs_type_info_t);
    w->pools = create_vector(ecs_pool_t);

    ecs_type_info_t zero = (ecs_type_info_t){0};
    vector_push_back(&w->types, &zero);

    uint32_t cap = desc.initial_entity_capacity ? desc.initial_entity_capacity : 1024u;
    vector_reserve(&w->entity_gen, cap);
    vector_reserve(&w->entity_alive, cap);
    vector_reserve(&w->entity_parent, cap);
    vector_reserve(&w->free_list, cap);

    w->required_tag_id = 0;
    w->root_entity = 0;

    ecs_world_ensure_entity_capacity_for_index(w, 1);

    ecs_vec_u32_set(&w->entity_gen, 0, 1);
    ecs_vec_u8_set(&w->entity_alive, 0, 1);
    ecs_vec_ent_set(&w->entity_parent, 0, 0);

    w->root_entity = ecs_entity_pack(0, 1);
}

void ecs_world_destroy(ecs_world_t *w)
{
    if (!w)
        return;

    ecs_type_info_t *ti = NULL;
    VECTOR_FOR_EACH(w->types, ecs_type_info_t, ti)
    {
        free(ti->name);
    }

    ecs_pool_t *p = NULL;
    VECTOR_FOR_EACH(w->pools, ecs_pool_t, p)
    {
        vector_free(&p->dense_entities);
        vector_free(&p->dense_data);
        vector_free(&p->sparse);
    }

    vector_free(&w->entity_gen);
    vector_free(&w->entity_alive);
    vector_free(&w->entity_parent);
    vector_free(&w->free_list);
    vector_free(&w->types);
    vector_free(&w->pools);

    *w = (ecs_world_t){0};
}

void ecs_world_set_required_tag(ecs_world_t *w, ecs_component_id_t tag_type_id)
{
    if (!w)
        return;
    w->required_tag_id = tag_type_id;
}

ecs_component_id_t ecs_world_required_tag(const ecs_world_t *w)
{
    if (!w)
        return 0;
    return w->required_tag_id;
}

void ecs_world_ensure_entity_capacity_for_index(ecs_world_t *w, uint32_t new_entity_count)
{
    if (w->entity_gen.size >= new_entity_count)
        return;

    uint32_t gen0 = 0;
    uint8_t alive0 = 0;
    ecs_entity_t p0 = 0;

    vector_resize(&w->entity_gen, new_entity_count, &gen0);
    vector_resize(&w->entity_alive, new_entity_count, &alive0);
    vector_resize(&w->entity_parent, new_entity_count, &p0);

    ecs_pool_t *p = NULL;
    VECTOR_FOR_EACH(w->pools, ecs_pool_t, p)
    {
        uint32_t inv = ECS_INVALID_U32;
        vector_resize(&p->sparse, new_entity_count, &inv);
    }
}
