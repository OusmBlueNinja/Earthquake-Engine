#include "core/systems/ecs/scene_yaml.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "vendor/miniyaml.h"

#include "core/systems/ecs/ecs.h"
#include "core/systems/ecs/entity.h"
#include "core/systems/ecs/internal.h"
#include "core/types/vec3.h"
#include "core/types/vector.h"

#include "core/systems/ecs/components/c_tag.h"
#include "core/systems/ecs/components/c_transform.h"
#include "core/systems/ecs/components/c_mesh_renderer.h"
#include "core/systems/ecs/components/c_light.h"

#include "core/managers/asset_manager/asset_manager.h"

static int parse_vec3_brackets(const char *s, vec3 *out)
{
    if (!s || !out)
        return 0;

    while (*s == ' ' || *s == '\t')
        ++s;
    if (*s != '[')
        return 0;
    ++s;

    char *endp = NULL;
    float x = strtof(s, &endp);
    if (endp == s)
        return 0;
    s = endp;
    while (*s == ' ' || *s == '\t')
        ++s;
    if (*s == ',')
        ++s;

    float y = strtof(s, &endp);
    if (endp == s)
        return 0;
    s = endp;
    while (*s == ' ' || *s == '\t')
        ++s;
    if (*s == ',')
        ++s;

    float z = strtof(s, &endp);
    if (endp == s)
        return 0;
    s = endp;
    while (*s == ' ' || *s == '\t')
        ++s;
    if (*s != ']')
        return 0;

    out->x = x;
    out->y = y;
    out->z = z;
    return 1;
}

static void scene_clear_all_entities(ecs_world_t *w)
{
    if (!w)
        return;

    ecs_entity_t root = ecs_world_root(w);

    vector_t to_destroy = create_vector(ecs_entity_t);
    for (uint32_t i = 0; i < w->entity_alive.size; ++i)
    {
        if (!ecs_vec_u8_get(&w->entity_alive, i))
            continue;
        ecs_entity_t e = ecs_entity_pack(i, ecs_vec_u32_get(&w->entity_gen, i));
        if (e == root)
            continue;
        vector_push_back(&to_destroy, &e);
    }

    for (uint32_t i = 0; i < to_destroy.size; ++i)
    {
        ecs_entity_t e = *(ecs_entity_t *)vector_impl_at(&to_destroy, i);
        ecs_entity_destroy(w, e);
    }

    vector_free(&to_destroy);
}

static int scene_asset_path_optional(const asset_manager_t *am, ihandle_t h, char *out, uint32_t out_sz)
{
    if (!out || out_sz == 0)
        return 0;
    out[0] = 0;
    if (!am || !ihandle_is_valid(h))
        return 0;
    return asset_manager_get_path(am, h, out, out_sz) ? 1 : 0;
}

int ecs_scene_save_yaml_file(const ecs_world_t *w, const char *path, const asset_manager_t *am)
{
    if (!w || !path || !path[0])
        return 0;

    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;

    myaml_writer_t y;
    myaml_writer_init(&y, f);

    myaml_write_linef(&y, 0, "# Earthquake Scene (MiniYAML)");
    myaml_write_key_u32(&y, 0, "version", 1u);
    myaml_write_key(&y, 0, "entities");

    uint32_t *id_by_index = NULL;
    uint32_t id_cap = w->entity_alive.size;
    if (id_cap)
        id_by_index = (uint32_t *)calloc((size_t)id_cap, sizeof(uint32_t));

    uint32_t next_id = 1;
    ecs_entity_t root = ecs_world_root(w);

    for (uint32_t i = 0; i < w->entity_alive.size; ++i)
    {
        if (!ecs_vec_u8_get(&w->entity_alive, i))
            continue;
        ecs_entity_t e = ecs_entity_pack(i, ecs_vec_u32_get(&w->entity_gen, i));
        if (e == root)
            continue;
        if (id_by_index)
            id_by_index[i] = next_id++;
    }

    for (uint32_t i = 0; i < w->entity_alive.size; ++i)
    {
        if (!ecs_vec_u8_get(&w->entity_alive, i))
            continue;
        ecs_entity_t e = ecs_entity_pack(i, ecs_vec_u32_get(&w->entity_gen, i));
        if (e == root)
            continue;

        const uint32_t id = id_by_index ? id_by_index[i] : 0;

        ecs_entity_t p = ecs_entity_get_parent(w, e);
        uint32_t pid = 0;
        if (p != 0 && p != root)
        {
            uint32_t pi = ecs_entity_index(p);
            if (id_by_index && pi < id_cap)
                pid = id_by_index[pi];
        }

        myaml_write_linef(&y, 2, "- id: %u", (unsigned)id);
        myaml_write_key_u32(&y, 4, "parent", pid);

        c_tag_t *tag = ecs_get((ecs_world_t *)w, e, c_tag_t);
        if (tag)
        {
            myaml_write_key_str(&y, 4, "name", tag->name);
            myaml_write_key_u32(&y, 4, "layer", (uint32_t)tag->layer);
            myaml_write_key_bool(&y, 4, "visible", (int)(tag->visible != 0));
        }

        c_transform_t *tr = ecs_get((ecs_world_t *)w, e, c_transform_t);
        if (tr)
        {
            myaml_write_linef(&y, 4, "position: [%.9g, %.9g, %.9g]", (double)tr->position.x, (double)tr->position.y, (double)tr->position.z);
            myaml_write_linef(&y, 4, "rotation: [%.9g, %.9g, %.9g]", (double)tr->rotation.x, (double)tr->rotation.y, (double)tr->rotation.z);
            myaml_write_linef(&y, 4, "scale: [%.9g, %.9g, %.9g]", (double)tr->scale.x, (double)tr->scale.y, (double)tr->scale.z);
        }

        c_mesh_renderer_t *mr = ecs_get((ecs_world_t *)w, e, c_mesh_renderer_t);
        if (mr)
        {
            myaml_write_key_u32(&y, 4, "model_type", (uint32_t)mr->model.type);
            myaml_write_key_u32(&y, 4, "model_meta", (uint32_t)mr->model.meta);
            myaml_write_key_u32(&y, 4, "model_value", (uint32_t)mr->model.value);

            char pth[ASSET_DEBUG_PATH_MAX];
            if (scene_asset_path_optional(am, mr->model, pth, (uint32_t)sizeof(pth)))
                myaml_write_key_str(&y, 4, "model_path", pth);
        }

        c_light_t *l = ecs_get((ecs_world_t *)w, e, c_light_t);
        if (l)
        {
            myaml_write_key_i32(&y, 4, "light_type", (int32_t)l->type);
            myaml_write_linef(&y, 4, "light_color: [%.9g, %.9g, %.9g]", (double)l->color.x, (double)l->color.y, (double)l->color.z);
            myaml_write_key_f32(&y, 4, "light_intensity", l->intensity);
            myaml_write_key_f32(&y, 4, "light_radius", l->radius);
            myaml_write_key_f32(&y, 4, "light_range", l->range);
        }
    }

    free(id_by_index);
    fclose(f);
    return 1;
}

typedef struct scene_yaml_entity_t
{
    uint32_t id;
    uint32_t parent;

    char name[C_TAG_NAME_MAX];
    uint32_t layer;
    int visible_set;
    int visible;

    int has_transform;
    vec3 position;
    vec3 rotation;
    vec3 scale;

    int has_model;
    uint32_t model_type;
    uint32_t model_meta;
    uint32_t model_value;
    char model_path[ASSET_DEBUG_PATH_MAX];

    int has_light;
    int32_t light_type;
    vec3 light_color;
    float light_intensity;
    float light_radius;
    float light_range;
} scene_yaml_entity_t;

static void scene_yaml_entity_init(scene_yaml_entity_t *e)
{
    if (!e)
        return;
    memset(e, 0, sizeof(*e));
    e->visible_set = 0;
    e->visible = 1;
    e->scale = (vec3){1, 1, 1};
    e->model_path[0] = 0;
    e->light_color = (vec3){1, 1, 1};
}

int ecs_scene_load_yaml_file(ecs_world_t *w, const char *path, asset_manager_t *am)
{
    if (!w || !path || !path[0])
        return 0;

    myaml_reader_t r;
    if (!myaml_reader_load_file(&r, path))
        return 0;

    vector_t ents = create_vector(scene_yaml_entity_t);
    uint32_t max_id = 0;

    const char *line = NULL;
    int indent = 0;
    int is_seq = 0;
    int in_entities = 0;
    scene_yaml_entity_t cur;
    int have_cur = 0;

    while (myaml_next_line(&r, &line, &indent, &is_seq))
    {
        if (!line || !line[0])
            continue;

        if (!in_entities)
        {
            if (strcmp(line, "entities:") == 0)
            {
                in_entities = 1;
                continue;
            }
            continue;
        }

        if (indent < 2)
            break;

        if (indent == 2 && is_seq)
        {
            if (have_cur)
                vector_push_back(&ents, &cur);

            scene_yaml_entity_init(&cur);
            have_cur = 1;

            if (strncmp(line, "id:", 3) == 0)
            {
                uint32_t id = 0;
                if (myaml_parse_u32(line + 3, &id))
                {
                    cur.id = id;
                    if (id > max_id)
                        max_id = id;
                }
            }

            continue;
        }

        if (!have_cur)
            continue;

        if (indent != 4)
            continue;

        if (strncmp(line, "id:", 3) == 0)
        {
            uint32_t id = 0;
            if (myaml_parse_u32(line + 3, &id))
            {
                cur.id = id;
                if (id > max_id)
                    max_id = id;
            }
            continue;
        }
        if (strncmp(line, "parent:", 7) == 0)
        {
            (void)myaml_parse_u32(line + 7, &cur.parent);
            continue;
        }
        if (strncmp(line, "name:", 5) == 0)
        {
            char *s = (char *)(line + 5);
            while (*s == ' ' || *s == '\t')
                ++s;
            myaml_unquote_inplace(s);
            memset(cur.name, 0, sizeof(cur.name));
            size_t n = strlen(s);
            if (n >= sizeof(cur.name))
                n = sizeof(cur.name) - 1;
            memcpy(cur.name, s, n);
            continue;
        }
        if (strncmp(line, "layer:", 6) == 0)
        {
            (void)myaml_parse_u32(line + 6, &cur.layer);
            continue;
        }
        if (strncmp(line, "visible:", 8) == 0)
        {
            int v = 1;
            if (myaml_parse_bool01(line + 8, &v))
            {
                cur.visible_set = 1;
                cur.visible = v;
            }
            continue;
        }
        if (strncmp(line, "position:", 9) == 0)
        {
            if (parse_vec3_brackets(line + 9, &cur.position))
                cur.has_transform = 1;
            continue;
        }
        if (strncmp(line, "rotation:", 9) == 0)
        {
            if (parse_vec3_brackets(line + 9, &cur.rotation))
                cur.has_transform = 1;
            continue;
        }
        if (strncmp(line, "scale:", 6) == 0)
        {
            if (parse_vec3_brackets(line + 6, &cur.scale))
                cur.has_transform = 1;
            continue;
        }
        if (strncmp(line, "model_type:", 11) == 0)
        {
            if (myaml_parse_u32(line + 11, &cur.model_type))
                cur.has_model = 1;
            continue;
        }
        if (strncmp(line, "model_meta:", 11) == 0)
        {
            if (myaml_parse_u32(line + 11, &cur.model_meta))
                cur.has_model = 1;
            continue;
        }
        if (strncmp(line, "model_value:", 12) == 0)
        {
            if (myaml_parse_u32(line + 12, &cur.model_value))
                cur.has_model = 1;
            continue;
        }
        if (strncmp(line, "model_path:", 11) == 0)
        {
            char *s = (char *)(line + 11);
            while (*s == ' ' || *s == '\t')
                ++s;
            myaml_unquote_inplace(s);
            memset(cur.model_path, 0, sizeof(cur.model_path));
            size_t n = strlen(s);
            if (n >= sizeof(cur.model_path))
                n = sizeof(cur.model_path) - 1;
            memcpy(cur.model_path, s, n);
            cur.has_model = 1;
            continue;
        }
        if (strncmp(line, "light_type:", 11) == 0)
        {
            (void)myaml_parse_i32(line + 11, &cur.light_type);
            cur.has_light = 1;
            continue;
        }
        if (strncmp(line, "light_color:", 12) == 0)
        {
            if (parse_vec3_brackets(line + 12, &cur.light_color))
                cur.has_light = 1;
            continue;
        }
        if (strncmp(line, "light_intensity:", 16) == 0)
        {
            (void)myaml_parse_f32(line + 16, &cur.light_intensity);
            cur.has_light = 1;
            continue;
        }
        if (strncmp(line, "light_radius:", 13) == 0)
        {
            (void)myaml_parse_f32(line + 13, &cur.light_radius);
            cur.has_light = 1;
            continue;
        }
        if (strncmp(line, "light_range:", 12) == 0)
        {
            (void)myaml_parse_f32(line + 12, &cur.light_range);
            cur.has_light = 1;
            continue;
        }
    }

    if (have_cur)
        vector_push_back(&ents, &cur);

    myaml_reader_free(&r);

    scene_clear_all_entities(w);

    ecs_entity_t root = ecs_world_root(w);
    vector_t id_to_entity = create_vector(ecs_entity_t);
    ecs_entity_t z = 0;
    vector_resize(&id_to_entity, max_id + 1, &z);

    for (uint32_t i = 0; i < ents.size; ++i)
    {
        scene_yaml_entity_t *se = (scene_yaml_entity_t *)vector_impl_at(&ents, i);
        if (!se || se->id == 0 || se->id >= id_to_entity.size)
            continue;

        ecs_entity_t e = ecs_entity_create(w);
        *(ecs_entity_t *)vector_impl_at(&id_to_entity, se->id) = e;

        c_tag_t *tag = ecs_get(w, e, c_tag_t);
        if (tag)
        {
            if (se->name[0])
            {
                memset(tag->name, 0, sizeof(tag->name));
                memcpy(tag->name, se->name, strlen(se->name) < (sizeof(tag->name) - 1) ? strlen(se->name) : (sizeof(tag->name) - 1));
            }
            tag->layer = (uint16_t)(se->layer & 0xFFFFu);
            if (se->visible_set)
                tag->visible = (uint8_t)(se->visible ? 1u : 0u);
        }

        if (se->has_transform)
        {
            c_transform_t *tr = ecs_add(w, e, c_transform_t);
            if (tr)
            {
                tr->position = se->position;
                tr->rotation = se->rotation;
                tr->scale = se->scale;
                tr->base.entity = e;
            }
        }

        if (se->has_model)
        {
            c_mesh_renderer_t *mr = ecs_add(w, e, c_mesh_renderer_t);
            if (mr)
            {
                ihandle_t h = (ihandle_t){
                    .value = se->model_value,
                    .type = (ihandle_type_t)se->model_type,
                    .meta = (uint16_t)(se->model_meta & 0xFFFFu)};

                if (am && se->model_path[0])
                {
                    ihandle_t req = asset_manager_request(am, ASSET_MODEL, se->model_path);
                    if (ihandle_is_valid(req))
                        h = req;
                }

                mr->model = h;
                mr->base.entity = e;
            }
        }

        if (se->has_light)
        {
            c_light_t *l = ecs_add(w, e, c_light_t);
            if (l)
            {
                l->type = (light_type_t)se->light_type;
                l->color = se->light_color;
                l->intensity = se->light_intensity;
                l->radius = se->light_radius;
                l->range = se->light_range;
                l->base.entity = e;
            }
        }
    }

    for (uint32_t i = 0; i < ents.size; ++i)
    {
        scene_yaml_entity_t *se = (scene_yaml_entity_t *)vector_impl_at(&ents, i);
        if (!se || se->id == 0 || se->id >= id_to_entity.size)
            continue;

        ecs_entity_t e = *(ecs_entity_t *)vector_impl_at(&id_to_entity, se->id);
        if (!ecs_entity_is_alive(w, e))
            continue;

        ecs_entity_t p = root;
        if (se->parent != 0 && se->parent < id_to_entity.size)
        {
            ecs_entity_t pe = *(ecs_entity_t *)vector_impl_at(&id_to_entity, se->parent);
            if (ecs_entity_is_alive(w, pe))
                p = pe;
        }

        (void)ecs_entity_set_parent(w, e, p);
    }

    vector_free(&id_to_entity);
    vector_free(&ents);
    return 1;
}
