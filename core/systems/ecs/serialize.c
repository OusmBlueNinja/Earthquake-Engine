#include "serialize.h"

#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "pool.h"

static void ecs_bytes_write(vector_t *b, const void *p, uint32_t n)
{
    uint32_t old = b->size;
    uint8_t z = 0;
    vector_resize(b, old + n, &z);
    memcpy((uint8_t *)b->data + old, p, n);
}

static void ecs_bytes_write_u32(vector_t *b, uint32_t v)
{
    ecs_bytes_write(b, &v, 4);
}

static void ecs_bytes_write_u64(vector_t *b, uint64_t v)
{
    ecs_bytes_write(b, &v, 8);
}

static void ecs_bytes_patch_u32(vector_t *b, uint32_t byte_offset, uint32_t v)
{
    if (!b || b->element_size != 1)
        return;
    if (byte_offset + 4 > b->size)
        return;
    memcpy((uint8_t *)b->data + byte_offset, &v, 4);
}

static int ecs_bytes_read_u32(const uint8_t *p, uint32_t size, uint32_t *io, uint32_t *out)
{
    if (*io + 4 > size)
        return 0;
    memcpy(out, p + *io, 4);
    *io += 4;
    return 1;
}

static int ecs_bytes_read_u64(const uint8_t *p, uint32_t size, uint32_t *io, uint64_t *out)
{
    if (*io + 8 > size)
        return 0;
    memcpy(out, p + *io, 8);
    *io += 8;
    return 1;
}

static int ecs_bytes_read_bytes(const uint8_t *p, uint32_t size, uint32_t *io, void *out, uint32_t n)
{
    if (*io + n > size)
        return 0;
    memcpy(out, p + *io, n);
    *io += n;
    return 1;
}

static int ecs_default_save_raw(const void *component, const ecs_type_info_t *ti, vector_t *out)
{
    uint32_t base_off = ti->base_offset;
    uint32_t base_sz = (uint32_t)sizeof(base_component_t);

    if (base_off > 0)
        ecs_bytes_write(out, component, base_off);

    uint32_t tail_off = base_off + base_sz;
    if (tail_off < ti->size)
        ecs_bytes_write(out, (const uint8_t *)component + tail_off, ti->size - tail_off);

    return 1;
}

static int ecs_default_load_raw(void *component, const ecs_type_info_t *ti, const uint8_t *payload, uint32_t payload_size)
{
    uint32_t base_off = ti->base_offset;
    uint32_t base_sz = (uint32_t)sizeof(base_component_t);

    uint32_t need = 0;
    if (base_off > 0)
        need += base_off;

    uint32_t tail_off = base_off + base_sz;
    if (tail_off < ti->size)
        need += ti->size - tail_off;

    if (payload_size != need)
        return 0;

    uint32_t o = 0;

    if (base_off > 0)
    {
        memcpy(component, payload + o, base_off);
        o += base_off;
    }

    if (tail_off < ti->size)
    {
        memcpy((uint8_t *)component + tail_off, payload + o, ti->size - tail_off);
        o += ti->size - tail_off;
    }

    return 1;
}

static uint32_t ecs_alive_entity_count(const ecs_world_t *w)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < w->entity_alive.size; ++i)
        n += ecs_vec_u8_get(&w->entity_alive, i) ? 1u : 0u;
    return n;
}

int ecs_scene_save_to_memory(const ecs_world_t *w, vector_t *out_bytes)
{
    if (!w || !out_bytes)
        return 0;

    *out_bytes = create_vector(uint8_t);

    uint32_t magic = 0x314E4353u;
    uint32_t version = 1u;

    ecs_bytes_write_u32(out_bytes, magic);
    ecs_bytes_write_u32(out_bytes, version);

    ecs_bytes_write_u32(out_bytes, w->required_tag_id);

    uint32_t ecount = ecs_alive_entity_count(w);
    ecs_bytes_write_u32(out_bytes, ecount);

    for (uint32_t i = 0; i < w->entity_alive.size; ++i)
    {
        if (!ecs_vec_u8_get(&w->entity_alive, i))
            continue;

        uint32_t gen = ecs_vec_u32_get(&w->entity_gen, i);
        ecs_bytes_write_u32(out_bytes, i);
        ecs_bytes_write_u32(out_bytes, gen);
    }

    uint32_t type_count = w->types.size > 1 ? (w->types.size - 1) : 0;
    ecs_bytes_write_u32(out_bytes, type_count);

    for (ecs_component_id_t type_id = 1; type_id < w->types.size; ++type_id)
    {
        const ecs_type_info_t *ti = (const ecs_type_info_t *)vector_impl_at((vector_t *)&w->types, type_id);

        uint16_t name_len = (uint16_t)(ti->name ? strlen(ti->name) : 0);
        ecs_bytes_write(out_bytes, &name_len, 2);
        if (name_len)
            ecs_bytes_write(out_bytes, ti->name, name_len);

        uint32_t comp_count = ecs_count_raw(w, type_id);
        ecs_bytes_write_u32(out_bytes, comp_count);

        if (!comp_count)
            continue;

        ecs_pool_t *p = NULL;
        ecs_pool_t *it = NULL;
        VECTOR_FOR_EACH(w->pools, ecs_pool_t, it)
        {
            if (it->type_id == type_id)
            {
                p = it;
                break;
            }
        }
        if (!p)
            return 0;

        for (uint32_t di = 0; di < p->dense_entities.size; ++di)
        {
            uint32_t ent_index = *(uint32_t *)vector_impl_at(&p->dense_entities, di);
            ecs_bytes_write_u32(out_bytes, ent_index);

            uint32_t size_pos = out_bytes->size;
            ecs_bytes_write_u32(out_bytes, 0);

            uint32_t start = out_bytes->size;

            const uint8_t *comp = (const uint8_t *)p->dense_data.data + (size_t)di * ti->size;
            const base_component_t *b = (const base_component_t *)(comp + ti->base_offset);

            if (b->save_fn)
            {
                if (!b->save_fn(comp, out_bytes))
                    return 0;
            }
            else if (ti->save_fn)
            {
                if (!ti->save_fn(comp, out_bytes))
                    return 0;
            }
            else
            {
                if (!ecs_default_save_raw(comp, ti, out_bytes))
                    return 0;
            }

            uint32_t end = out_bytes->size;
            ecs_bytes_patch_u32(out_bytes, size_pos, end - start);
        }
    }

    return 1;
}

static ecs_type_info_t *ecs_find_type_by_name(ecs_world_t *w, const char *name, uint16_t name_len)
{
    for (uint32_t i = 1; i < w->types.size; ++i)
    {
        ecs_type_info_t *ti = (ecs_type_info_t *)vector_impl_at(&w->types, i);
        if (!ti->name)
            continue;
        if (strlen(ti->name) != (size_t)name_len)
            continue;
        if (memcmp(ti->name, name, name_len) == 0)
            return ti;
    }
    return NULL;
}

static void ecs_world_clear_runtime(ecs_world_t *w)
{
    if (!w)
        return;

    uint8_t z8 = 0;
    uint32_t z32 = 0;

    vector_resize(&w->entity_alive, w->entity_alive.size, &z8);
    vector_resize(&w->entity_gen, w->entity_gen.size, &z32);

    vector_clear(&w->free_list);

    ecs_pool_t *p = NULL;
    VECTOR_FOR_EACH(w->pools, ecs_pool_t, p)
    {
        vector_clear(&p->dense_entities);
        vector_clear(&p->dense_data);

        uint32_t inv = ECS_INVALID_U32;
        vector_resize(&p->sparse, p->sparse.size, &inv);
    }
}

int ecs_scene_load_from_memory(ecs_world_t *w, const uint8_t *bytes, uint32_t size)
{
    if (!w || !bytes || !size)
        return 0;

    ecs_world_clear_runtime(w);

    uint32_t o = 0;
    uint32_t magic = 0;
    uint32_t version = 0;

    if (!ecs_bytes_read_u32(bytes, size, &o, &magic))
        return 0;
    if (!ecs_bytes_read_u32(bytes, size, &o, &version))
        return 0;

    if (magic != 0x314E4353u || version != 1u)
        return 0;

    uint32_t file_required_tag = 0;
    if (!ecs_bytes_read_u32(bytes, size, &o, &file_required_tag))
        return 0;

    uint32_t ecount = 0;
    if (!ecs_bytes_read_u32(bytes, size, &o, &ecount))
        return 0;

    uint32_t max_index = 0;
    uint32_t ent_list_off = o;

    for (uint32_t i = 0; i < ecount; ++i)
    {
        uint32_t idx = 0;
        uint32_t gen = 0;
        if (!ecs_bytes_read_u32(bytes, size, &o, &idx))
            return 0;
        if (!ecs_bytes_read_u32(bytes, size, &o, &gen))
            return 0;
        if (idx > max_index)
            max_index = idx;
    }

    ecs_world_ensure_entity_capacity_for_index(w, max_index + 1);

    for (uint32_t i = 0; i < w->entity_alive.size; ++i)
    {
        ecs_vec_u8_set(&w->entity_alive, i, 0);
        ecs_vec_u32_set(&w->entity_gen, i, 1);
    }

    vector_clear(&w->free_list);

    o = ent_list_off;

    for (uint32_t i = 0; i < ecount; ++i)
    {
        uint32_t idx = 0;
        uint32_t gen = 0;
        if (!ecs_bytes_read_u32(bytes, size, &o, &idx))
            return 0;
        if (!ecs_bytes_read_u32(bytes, size, &o, &gen))
            return 0;

        if (idx >= w->entity_alive.size)
            return 0;

        if (!gen)
            gen = 1;

        ecs_vec_u8_set(&w->entity_alive, idx, 1);
        ecs_vec_u32_set(&w->entity_gen, idx, gen);
    }

    for (uint32_t i = 0; i < w->entity_alive.size; ++i)
    {
        if (!ecs_vec_u8_get(&w->entity_alive, i))
            vector_push_back(&w->free_list, &i);
    }

    uint32_t type_count = 0;
    if (!ecs_bytes_read_u32(bytes, size, &o, &type_count))
        return 0;

    for (uint32_t tii = 0; tii < type_count; ++tii)
    {
        uint16_t name_len = 0;
        if (!ecs_bytes_read_bytes(bytes, size, &o, &name_len, 2))
            return 0;

        const uint8_t *name_ptr = NULL;
        if (name_len)
        {
            if (o + name_len > size)
                return 0;
            name_ptr = bytes + o;
            o += name_len;
        }

        uint32_t comp_count = 0;
        if (!ecs_bytes_read_u32(bytes, size, &o, &comp_count))
            return 0;

        ecs_type_info_t *ti = NULL;
        if (name_len)
            ti = ecs_find_type_by_name(w, (const char *)name_ptr, name_len);

        for (uint32_t ci = 0; ci < comp_count; ++ci)
        {
            uint32_t ent_index = 0;
            uint32_t payload_size = 0;

            if (!ecs_bytes_read_u32(bytes, size, &o, &ent_index))
                return 0;
            if (!ecs_bytes_read_u32(bytes, size, &o, &payload_size))
                return 0;

            if (o + payload_size > size)
                return 0;

            const uint8_t *payload = bytes + o;
            o += payload_size;

            if (!ti)
                continue;

            ecs_entity_t e = ecs_entity_pack(ent_index, ecs_vec_u32_get(&w->entity_gen, ent_index));
            void *comp = ecs_add_raw(w, e, (ecs_component_id_t)(ti - (ecs_type_info_t *)w->types.data));

            if (!comp)
                return 0;

            base_component_t *b = (base_component_t *)((uint8_t *)comp + ti->base_offset);

            if (b->load_fn)
            {
                if (!b->load_fn(comp, payload, payload_size))
                    return 0;
            }
            else if (ti->load_fn)
            {
                if (!ti->load_fn(comp, payload, payload_size))
                    return 0;
            }
            else
            {
                if (!ecs_default_load_raw(comp, ti, payload, payload_size))
                    return 0;
            }
        }
    }

    if (file_required_tag && file_required_tag < w->types.size)
        w->required_tag_id = file_required_tag;

    return 1;
}

int ecs_scene_save_file(const ecs_world_t *w, const char *path)
{
    if (!w || !path || !path[0])
        return 0;

    vector_t bytes = (vector_t){0};
    if (!ecs_scene_save_to_memory(w, &bytes))
        return 0;

    FILE *f = fopen(path, "wb");
    if (!f)
    {
        vector_free(&bytes);
        return 0;
    }

    fwrite(bytes.data, 1, bytes.size, f);
    fclose(f);

    vector_free(&bytes);
    return 1;
}

int ecs_scene_load_file(ecs_world_t *w, const char *path)
{
    if (!w || !path || !path[0])
        return 0;

    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0)
    {
        fclose(f);
        return 0;
    }

    vector_t bytes = create_vector(uint8_t);
    uint8_t z = 0;
    vector_resize(&bytes, (uint32_t)sz, &z);

    size_t got = fread(bytes.data, 1, bytes.size, f);
    fclose(f);

    int ok = 0;
    if (got == bytes.size)
        ok = ecs_scene_load_from_memory(w, (const uint8_t *)bytes.data, bytes.size);

    vector_free(&bytes);
    return ok;
}
