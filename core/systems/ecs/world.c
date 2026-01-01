#include "world.h"
#include "internal.h"

void ecs_world_init(ecs_world_t *w, ecs_world_desc_t desc)
{
    if (!w)
        return;

    *w = (ecs_world_t){0};

    w->entity_gen = create_vector(uint32_t);
    w->entity_alive = create_vector(uint8_t);
    w->free_list = create_vector(uint32_t);

    w->types = create_vector(ecs_type_info_t);
    w->pools = create_vector(ecs_pool_t);

    ecs_type_info_t zero = (ecs_type_info_t){0};
    vector_push_back(&w->types, &zero);

    uint32_t cap = desc.initial_entity_capacity ? desc.initial_entity_capacity : 1024u;

    uint32_t gen0 = 0;
    uint8_t alive0 = 0;

    vector_resize(&w->entity_gen, cap, &gen0);
    vector_resize(&w->entity_alive, cap, &alive0);

    w->required_tag_id = 0;
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

    vector_resize(&w->entity_gen, new_entity_count, &gen0);
    vector_resize(&w->entity_alive, new_entity_count, &alive0);

    ecs_pool_t *p = NULL;
    VECTOR_FOR_EACH(w->pools, ecs_pool_t, p)
    {
        uint32_t inv = ECS_INVALID_U32;
        vector_resize(&p->sparse, new_entity_count, &inv);
    }
}
