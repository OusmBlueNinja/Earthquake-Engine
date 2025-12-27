#include "material.h"
#include <string.h>
#include <stdlib.h>
#include "utils/strdup.h"

static void ikv_write_vec3(ikv_node_t *parent, const char *key, vec3 v)
{
    ikv_node_t *o = ikv_object_add_object(parent, key);
    ikv_object_set_float(o, "x", (double)v.x);
    ikv_object_set_float(o, "y", (double)v.y);
    ikv_object_set_float(o, "z", (double)v.z);
}

static bool ikv_read_vec3(const ikv_node_t *parent, const char *key, vec3 *out)
{
    const ikv_node_t *o = ikv_object_get(parent, key);
    if (!o || o->type != IKV_OBJECT)
        return false;

    const ikv_node_t *nx = ikv_object_get(o, "x");
    const ikv_node_t *ny = ikv_object_get(o, "y");
    const ikv_node_t *nz = ikv_object_get(o, "z");
    if (!nx || !ny || !nz)
        return false;

    out->x = (float)ikv_as_float(nx);
    out->y = (float)ikv_as_float(ny);
    out->z = (float)ikv_as_float(nz);
    return true;
}

static void ikv_write_handle(ikv_node_t *parent, const char *key, ihandle_t h)
{
    ikv_node_t *o = ikv_object_add_object(parent, key);

    ikv_object_set_int(o, "type", (int64_t)ihandle_type(h));
    ikv_object_set_int(o, "meta", (int64_t)h.meta);
    ikv_object_set_int(o, "index", (int64_t)ihandle_index(h));
    ikv_object_set_int(o, "generation", (int64_t)ihandle_generation(h));
}

static bool ikv_read_handle(const ikv_node_t *parent, const char *key, ihandle_t *out)
{
    const ikv_node_t *o = ikv_object_get(parent, key);
    if (!o || o->type != IKV_OBJECT)
        return false;

    const ikv_node_t *nt = ikv_object_get(o, "type");
    const ikv_node_t *nm = ikv_object_get(o, "meta");
    const ikv_node_t *ni = ikv_object_get(o, "index");
    const ikv_node_t *ng = ikv_object_get(o, "generation");
    if (!nt || !nm || !ni || !ng)
        return false;

    ihandle_type_t type = (ihandle_type_t)ikv_as_int(nt);
    uint16_t meta = (uint16_t)ikv_as_int(nm);
    uint16_t index = (uint16_t)ikv_as_int(ni);
    uint16_t gen = (uint16_t)ikv_as_int(ng);

    ihandle_t h = ihandle_make(type, index, gen);
    h = ihandle_with_meta(h, meta);

    *out = h;
    return true;
}

asset_material_t material_make_default(uint8_t shader_id)
{
    asset_material_t m;
    memset(&m, 0, sizeof(m));
    m.shader_id = shader_id;
    m.flags = 0;
    m.name = dup_cstr("iDefMat");

    material_set_flag(&m, MAT_FLAG_ALPHA_CUTOUT, false);
    material_set_flag(&m, MAT_FLAG_DOUBLE_SIDED, false);

    m.albedo = (vec3){1.0f, 1.0f, 1.0f};
    m.emissive = (vec3){0.0f, 0.0f, 0.0f};
    m.roughness = 1.0f;
    m.metallic = 0.0f;
    m.opacity = 1.0f;

    m.alpha_cutoff - 0.1f;

    m.normal_strength = 1.0f;
    m.height_scale = 0.03f;
    m.height_steps = 24;

    m.albedo_tex = ihandle_invalid();
    m.normal_tex = ihandle_invalid();
    m.metallic_tex = ihandle_invalid();
    m.roughness_tex = ihandle_invalid();
    m.emissive_tex = ihandle_invalid();
    m.occlusion_tex = ihandle_invalid();
    m.height_tex = ihandle_invalid();
    m.arm_tex = ihandle_invalid();
    return m;
}

ikv_node_t *material_to_ikv(const asset_material_t *m, const char *key)
{
    ikv_node_t *root = ikv_create_object(key ? key : "material");

    ikv_object_set_int(root, "shader_id", (int64_t)m->shader_id);
    ikv_object_set_int(root, "flags", (int64_t)m->flags);

    ikv_write_vec3(root, "albedo", m->albedo);
    ikv_write_vec3(root, "emissive", m->emissive);

    ikv_object_set_float(root, "roughness", (double)m->roughness);
    ikv_object_set_float(root, "metallic", (double)m->metallic);
    ikv_object_set_float(root, "opacity", (double)m->opacity);

    ikv_object_set_float(root, "normal_strength", (double)m->normal_strength);
    ikv_object_set_float(root, "height_scale", (double)m->height_scale);
    ikv_object_set_int(root, "height_steps", (int64_t)m->height_steps);

    ikv_write_handle(root, "albedo_tex", m->albedo_tex);
    ikv_write_handle(root, "normal_tex", m->normal_tex);
    ikv_write_handle(root, "metallic_tex", m->metallic_tex);
    ikv_write_handle(root, "roughness_tex", m->roughness_tex);
    ikv_write_handle(root, "emissive_tex", m->emissive_tex);
    ikv_write_handle(root, "occlusion_tex", m->occlusion_tex);
    ikv_write_handle(root, "height_tex", m->height_tex);
    ikv_write_handle(root, "arm_tex", m->arm_tex);

    return root;
}

bool material_from_ikv(const ikv_node_t *node, asset_material_t *out)
{
    if (!node || node->type != IKV_OBJECT || !out)
        return false;

    memset(out, 0, sizeof(*out));

    const ikv_node_t *ns = ikv_object_get(node, "shader_id");
    const ikv_node_t *nf = ikv_object_get(node, "flags");
    if (!ns || !nf)
        return false;

    out->shader_id = (uint8_t)ikv_as_int(ns);
    out->flags = (uint32_t)ikv_as_int(nf);

    if (!ikv_read_vec3(node, "albedo", &out->albedo))
        return false;
    if (!ikv_read_vec3(node, "emissive", &out->emissive))
        return false;

    const ikv_node_t *nr = ikv_object_get(node, "roughness");
    const ikv_node_t *nm = ikv_object_get(node, "metallic");
    const ikv_node_t *no = ikv_object_get(node, "opacity");
    const ikv_node_t *nns = ikv_object_get(node, "normal_strength");
    const ikv_node_t *nhs = ikv_object_get(node, "height_scale");
    const ikv_node_t *nht = ikv_object_get(node, "height_steps");
    if (!nr || !nm || !no || !nns || !nhs || !nht)
        return false;

    out->roughness = (float)ikv_as_float(nr);
    out->metallic = (float)ikv_as_float(nm);
    out->opacity = (float)ikv_as_float(no);
    out->normal_strength = (float)ikv_as_float(nns);
    out->height_scale = (float)ikv_as_float(nhs);
    out->height_steps = (int)ikv_as_int(nht);

    if (!ikv_read_handle(node, "albedo_tex", &out->albedo_tex))
        return false;
    if (!ikv_read_handle(node, "normal_tex", &out->normal_tex))
        return false;
    if (!ikv_read_handle(node, "metallic_tex", &out->metallic_tex))
        return false;
    if (!ikv_read_handle(node, "roughness_tex", &out->roughness_tex))
        return false;
    if (!ikv_read_handle(node, "emissive_tex", &out->emissive_tex))
        return false;
    if (!ikv_read_handle(node, "occlusion_tex", &out->occlusion_tex))
        return false;
    if (!ikv_read_handle(node, "height_tex", &out->height_tex))
        return false;
    if (!ikv_read_handle(node, "arm_tex", &out->arm_tex))
        return false;

    return true;
}

bool material_save_file(const char *path, const asset_material_t *m)
{
    ikv_node_t *root = material_to_ikv(m, "material");
    if (!root)
        return false;

    bool ok = ikv_write_file(path, root);
    ikv_free(root);
    return ok;
}

bool material_load_file(const char *path, asset_material_t *out)
{
    ikv_node_t *root = ikv_parse_file(path);
    if (!root)
        return false;

    bool ok = material_from_ikv(root, out);
    ikv_free(root);
    return ok;
}

static asset_material_t *material_create_solid(uint8_t shader_id, vec3 albedo)
{
    asset_material_t *mat = (asset_material_t *)calloc(1, sizeof(asset_material_t));
    if (!mat)
        return NULL;

    mat->shader_id = shader_id;
    mat->flags = 0;
    strcpy(mat->name, "iSolidMat");

    mat->albedo = albedo;
    mat->emissive = (vec3){0.0f, 0.0f, 0.0f};

    mat->roughness = 0.6f;
    mat->metallic = 0.1f;
    mat->opacity = 1.0f;

    mat->normal_strength = 1.0f;
    mat->height_scale = 0.05f;
    mat->height_steps = 16;

    mat->albedo_tex = ihandle_invalid();
    mat->normal_tex = ihandle_invalid();
    mat->metallic_tex = ihandle_invalid();
    mat->roughness_tex = ihandle_invalid();
    mat->emissive_tex = ihandle_invalid();
    mat->occlusion_tex = ihandle_invalid();
    mat->height_tex = ihandle_invalid();
    mat->arm_tex = ihandle_invalid();

    return mat;
}

void material_set_flag(asset_material_t *m, material_flags_t flag, bool state)
{
    if (!m)
        return;
    if (state)
        m->flags = (material_flags_t)(m->flags | flag);
    else
        m->flags = (material_flags_t)(m->flags & (material_flags_t)~flag);
}
