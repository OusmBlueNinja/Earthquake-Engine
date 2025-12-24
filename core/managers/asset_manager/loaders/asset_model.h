#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <math.h>

#include "asset_manager/asset_manager.h"
#include "asset_manager/asset_types/model.h"
#include "asset_manager/asset_types/material.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

typedef struct obj_v3_t
{
    float x, y, z;
} obj_v3_t;
typedef struct obj_v2_t
{
    float x, y;
} obj_v2_t;

typedef struct grow_vtx_t
{
    model_vertex_t *p;
    uint32_t count;
    uint32_t cap;
} grow_vtx_t;

typedef struct grow_u32_t
{
    uint32_t *p;
    uint32_t count;
    uint32_t cap;
} grow_u32_t;

static bool mdl_grow_vtx_reserve(grow_vtx_t *g, uint32_t add)
{
    uint32_t need = g->count + add;
    if (need <= g->cap)
        return true;
    uint32_t ncap = g->cap ? g->cap : 1024;
    while (ncap < need)
        ncap *= 2;
    model_vertex_t *np = (model_vertex_t *)realloc(g->p, (size_t)ncap * sizeof(model_vertex_t));
    if (!np)
        return false;
    g->p = np;
    g->cap = ncap;
    return true;
}

static bool mdl_grow_u32_reserve(grow_u32_t *g, uint32_t add)
{
    uint32_t need = g->count + add;
    if (need <= g->cap)
        return true;
    uint32_t ncap = g->cap ? g->cap : 2048;
    while (ncap < need)
        ncap *= 2;
    uint32_t *np = (uint32_t *)realloc(g->p, (size_t)ncap * sizeof(uint32_t));
    if (!np)
        return false;
    g->p = np;
    g->cap = ncap;
    return true;
}

static void mdl_grow_vtx_free(grow_vtx_t *g)
{
    free(g->p);
    memset(g, 0, sizeof(*g));
}

static void mdl_grow_u32_free(grow_u32_t *g)
{
    free(g->p);
    memset(g, 0, sizeof(*g));
}

static char *mdl_path_dirname_dup(const char *path)
{
    if (!path)
        return NULL;
    size_t n = strlen(path);
    size_t cut = 0;
    for (size_t i = 0; i < n; ++i)
    {
        char c = path[i];
        if (c == '/' || c == '\\')
            cut = i + 1;
    }
    char *out = (char *)malloc(cut + 1);
    if (!out)
        return NULL;
    memcpy(out, path, cut);
    out[cut] = 0;
    return out;
}

static char *mdl_path_join_dup(const char *a, const char *b)
{
    if (!a || !b)
        return NULL;
    size_t na = strlen(a);
    size_t nb = strlen(b);
    char need = 0;
    if (na > 0)
    {
        char c = a[na - 1];
        if (c != '/' && c != '\\')
            need = 1;
    }
    char *out = (char *)malloc(na + (size_t)need + nb + 1);
    if (!out)
        return NULL;
    memcpy(out, a, na);
    if (need)
        out[na] = '/';
    memcpy(out + na + (size_t)need, b, nb);
    out[na + (size_t)need + nb] = 0;
    return out;
}

static char *mdl_strdup_trim(const char *s)
{
    if (!s)
        return NULL;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    size_t n = strlen(s);
    while (n > 0)
    {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            n--;
        else
            break;
    }
    char *out = (char *)malloc(n + 1);
    if (!out)
        return NULL;
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}

static int mdl_str_eq_ci(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b)
    {
        char c1 = *a++;
        char c2 = *b++;
        if (c1 >= 'A' && c1 <= 'Z')
            c1 = (char)(c1 - 'A' + 'a');
        if (c2 >= 'A' && c2 <= 'Z')
            c2 = (char)(c2 - 'A' + 'a');
        if (c1 != c2)
            return 0;
    }
    return *a == 0 && *b == 0;
}

static int mdl_str_contains_ci(const char *hay, const char *needle)
{
    if (!hay || !needle)
        return 0;
    size_t nh = strlen(hay);
    size_t nn = strlen(needle);
    if (!nn || nn > nh)
        return 0;

    for (size_t i = 0; i + nn <= nh; ++i)
    {
        int ok = 1;
        for (size_t j = 0; j < nn; ++j)
        {
            char a = hay[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z')
                a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = (char)(b - 'A' + 'a');
            if (a != b)
            {
                ok = 0;
                break;
            }
        }
        if (ok)
            return 1;
    }
    return 0;
}

static float mdl_clamp01(float x)
{
    if (x < 0.0f)
        return 0.0f;
    if (x > 1.0f)
        return 1.0f;
    return x;
}

static float mdl_ns_to_roughness(float ns)
{
    if (ns < 1.0f)
        return 1.0f;
    float r = sqrtf(2.0f / (ns + 2.0f));
    return mdl_clamp01(r);
}

static void mdl_set_tex(asset_manager_t *am, ihandle_t *dst, const char *mtl_path, const char *file)
{
    if (!dst)
        return;
    *dst = ihandle_invalid();
    if (!am || !file || !file[0])
        return;

    char *dir = mdl_path_dirname_dup(mtl_path);
    char *full = dir ? mdl_path_join_dup(dir, file) : mdl_strdup_trim(file);
    free(dir);
    if (!full)
        return;

    *dst = asset_manager_request(am, ASSET_IMAGE, full);
    free(full);
}

static const char *mdl_mtl_parse_tex_path(char *args)
{
    if (!args)
        return NULL;
    while (*args == ' ' || *args == '\t')
        args++;
    if (!*args)
        return NULL;

    char *p = args;
    const char *last = NULL;

    while (*p)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;

        if (*p == '-')
        {
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
                p++;
            while (*p == ' ' || *p == '\t')
                p++;
            if (!*p)
                break;

            if (*p != '-')
            {
                while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
                    p++;
            }
            continue;
        }

        last = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            p++;
        if (*p)
        {
            *p = 0;
            p++;
        }
    }

    return last;
}

static void mdl_submesh_zero(model_cpu_submesh_t *sm)
{
    memset(sm, 0, sizeof(*sm));
    sm->material = ihandle_invalid();
}

static bool mdl_obj_quick_verify(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext && (!strcmp(ext, ".obj") || !strcmp(ext, ".OBJ")))
        return true;

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;

    bool saw_v = false;
    bool saw_f = false;

    char *p = buf;
    while (*p)
    {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (!*p)
            break;

        if (*p == '#')
        {
            while (*p && *p != '\n')
                p++;
            continue;
        }

        if (p[0] == 'v' && (p[1] == ' ' || p[1] == 't' || p[1] == 'n'))
            saw_v = true;

        if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t'))
            saw_f = true;

        while (*p && *p != '\n')
            p++;
    }

    return saw_v && saw_f;
}

static bool mdl_parse_obj_index(const char *tok, int *vi, int *vti, int *vni)
{
    *vi = *vti = *vni = 0;
    if (!tok || !tok[0])
        return false;

    const char *p = tok;
    char *end = NULL;

    long a = strtol(p, &end, 10);
    if (end == p)
        return false;
    *vi = (int)a;

    if (*end != '/')
        return true;
    p = end + 1;

    if (*p == '/')
    {
        p++;
        long c = strtol(p, &end, 10);
        if (end != p)
            *vni = (int)c;
        return true;
    }

    long b = strtol(p, &end, 10);
    if (end != p)
        *vti = (int)b;

    if (*end != '/')
        return true;
    p = end + 1;

    long c = strtol(p, &end, 10);
    if (end != p)
        *vni = (int)c;

    return true;
}

static uint32_t mdl_obj_fix_index(int idx, uint32_t count)
{
    if (idx > 0)
        return (uint32_t)(idx - 1);
    if (idx < 0)
        return (uint32_t)((int)count + idx);
    return 0;
}

static bool mdl_obj_load_to_raw_fast(const char *path, model_raw_t *out_raw, char **out_mtllib)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    vector_t positions = vector_impl_create_vector(sizeof(obj_v3_t));
    vector_t texcoords = vector_impl_create_vector(sizeof(obj_v2_t));
    vector_t normals = vector_impl_create_vector(sizeof(obj_v3_t));

    model_raw_t raw = model_raw_make();

    model_cpu_submesh_t cur;
    mdl_submesh_zero(&cur);

    grow_vtx_t gv = {0};
    grow_u32_t gi = {0};

    char *mtllib = NULL;
    char line[4096];

    while (fgets(line, sizeof(line), f))
    {
        char *s = line;
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s || *s == '\r' || *s == '\n' || *s == '#')
            continue;

        if (s[0] == 'v' && s[1] == ' ')
        {
            obj_v3_t v;
            if (sscanf(s + 2, "%f %f %f", &v.x, &v.y, &v.z) == 3)
                vector_impl_push_back(&positions, &v);
            continue;
        }

        if (s[0] == 'v' && s[1] == 't' && (s[2] == ' ' || s[2] == '\t'))
        {
            obj_v2_t vt;
            if (sscanf(s + 3, "%f %f", &vt.x, &vt.y) >= 2)
                vector_impl_push_back(&texcoords, &vt);
            continue;
        }

        if (s[0] == 'v' && s[1] == 'n' && (s[2] == ' ' || s[2] == '\t'))
        {
            obj_v3_t vn;
            if (sscanf(s + 3, "%f %f %f", &vn.x, &vn.y, &vn.z) == 3)
                vector_impl_push_back(&normals, &vn);
            continue;
        }

        if (!strncmp(s, "mtllib", 6) && (s[6] == ' ' || s[6] == '\t'))
        {
            free(mtllib);
            mtllib = mdl_strdup_trim(s + 6);
            continue;
        }

        if (!strncmp(s, "usemtl", 6) && (s[6] == ' ' || s[6] == '\t'))
        {
            if (gi.count > 0)
            {
                cur.vertices = gv.p;
                cur.vertex_count = gv.count;
                cur.indices = gi.p;
                cur.index_count = gi.count;
                vector_impl_push_back(&raw.submeshes, &cur);
                gv = (grow_vtx_t){0};
                gi = (grow_u32_t){0};
            }
            else
            {
                free(cur.material_name);
                cur.material_name = NULL;
            }

            mdl_submesh_zero(&cur);
            cur.material_name = mdl_strdup_trim(s + 6);
            continue;
        }

        if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t'))
        {
            char *p = s + 1;
            while (*p == ' ' || *p == '\t')
                p++;

            char *tok[64];
            int tc = 0;

            while (*p && *p != '\r' && *p != '\n')
            {
                while (*p == ' ' || *p == '\t')
                    p++;
                if (!*p || *p == '\r' || *p == '\n')
                    break;
                if (tc >= 64)
                    break;
                tok[tc++] = p;
                while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
                    p++;
                if (*p)
                {
                    *p = 0;
                    p++;
                }
            }

            if (tc < 3)
                continue;

            int avi, avt, avn;
            if (!mdl_parse_obj_index(tok[0], &avi, &avt, &avn))
                continue;

            uint32_t api = mdl_obj_fix_index(avi, (uint32_t)positions.size);
            obj_v3_t ap = *(obj_v3_t *)vector_impl_at(&positions, api);

            obj_v2_t at = (obj_v2_t){0, 0};
            obj_v3_t an = (obj_v3_t){0, 0, 0};

            if (avt)
            {
                uint32_t ati = mdl_obj_fix_index(avt, (uint32_t)texcoords.size);
                at = *(obj_v2_t *)vector_impl_at(&texcoords, ati);
            }

            if (avn)
            {
                uint32_t ani = mdl_obj_fix_index(avn, (uint32_t)normals.size);
                an = *(obj_v3_t *)vector_impl_at(&normals, ani);
            }

            for (int i = 1; i < tc - 1; ++i)
            {
                int bvi, bvt, bvn;
                int cvi, cvt, cvn;

                if (!mdl_parse_obj_index(tok[i], &bvi, &bvt, &bvn))
                    continue;
                if (!mdl_parse_obj_index(tok[i + 1], &cvi, &cvt, &cvn))
                    continue;

                uint32_t bpi = mdl_obj_fix_index(bvi, (uint32_t)positions.size);
                uint32_t cpi = mdl_obj_fix_index(cvi, (uint32_t)positions.size);

                obj_v3_t bp = *(obj_v3_t *)vector_impl_at(&positions, bpi);
                obj_v3_t cp = *(obj_v3_t *)vector_impl_at(&positions, cpi);

                obj_v2_t bt = (obj_v2_t){0, 0};
                obj_v2_t ct = (obj_v2_t){0, 0};

                obj_v3_t bn = (obj_v3_t){0, 0, 0};
                obj_v3_t cn = (obj_v3_t){0, 0, 0};

                if (bvt)
                {
                    uint32_t bti = mdl_obj_fix_index(bvt, (uint32_t)texcoords.size);
                    bt = *(obj_v2_t *)vector_impl_at(&texcoords, bti);
                }

                if (cvt)
                {
                    uint32_t cti = mdl_obj_fix_index(cvt, (uint32_t)texcoords.size);
                    ct = *(obj_v2_t *)vector_impl_at(&texcoords, cti);
                }

                if (bvn)
                {
                    uint32_t bni = mdl_obj_fix_index(bvn, (uint32_t)normals.size);
                    bn = *(obj_v3_t *)vector_impl_at(&normals, bni);
                }

                if (cvn)
                {
                    uint32_t cni = mdl_obj_fix_index(cvn, (uint32_t)normals.size);
                    cn = *(obj_v3_t *)vector_impl_at(&normals, cni);
                }

                if (!mdl_grow_vtx_reserve(&gv, 3))
                {
                    fclose(f);
                    vector_impl_free(&positions);
                    vector_impl_free(&texcoords);
                    vector_impl_free(&normals);
                    model_raw_destroy(&raw);
                    free(mtllib);
                    mdl_grow_vtx_free(&gv);
                    mdl_grow_u32_free(&gi);
                    return false;
                }
                if (!mdl_grow_u32_reserve(&gi, 3))
                {
                    fclose(f);
                    vector_impl_free(&positions);
                    vector_impl_free(&texcoords);
                    vector_impl_free(&normals);
                    model_raw_destroy(&raw);
                    free(mtllib);
                    mdl_grow_vtx_free(&gv);
                    mdl_grow_u32_free(&gi);
                    return false;
                }

                model_vertex_t va;
                memset(&va, 0, sizeof(va));
                va.px = ap.x;
                va.py = ap.y;
                va.pz = ap.z;
                va.nx = an.x;
                va.ny = an.y;
                va.nz = an.z;
                va.u = at.x;
                va.v = 1.0f - at.y;

                model_vertex_t vb;
                memset(&vb, 0, sizeof(vb));
                vb.px = bp.x;
                vb.py = bp.y;
                vb.pz = bp.z;
                vb.nx = bn.x;
                vb.ny = bn.y;
                vb.nz = bn.z;
                vb.u = bt.x;
                vb.v = 1.0f - bt.y;

                model_vertex_t vc;
                memset(&vc, 0, sizeof(vc));
                vc.px = cp.x;
                vc.py = cp.y;
                vc.pz = cp.z;
                vc.nx = cn.x;
                vc.ny = cn.y;
                vc.nz = cn.z;
                vc.u = ct.x;
                vc.v = 1.0f - ct.y;

                uint32_t base = gv.count;

                gv.p[gv.count++] = va;
                gv.p[gv.count++] = vb;
                gv.p[gv.count++] = vc;

                gi.p[gi.count++] = base + 0;
                gi.p[gi.count++] = base + 1;
                gi.p[gi.count++] = base + 2;
            }

            continue;
        }
    }

    fclose(f);

    if (gi.count > 0)
    {
        cur.vertices = gv.p;
        cur.vertex_count = gv.count;
        cur.indices = gi.p;
        cur.index_count = gi.count;
        vector_impl_push_back(&raw.submeshes, &cur);
        gv = (grow_vtx_t){0};
        gi = (grow_u32_t){0};
    }
    else
    {
        free(cur.material_name);
        cur.material_name = NULL;
    }

    mdl_grow_vtx_free(&gv);
    mdl_grow_u32_free(&gi);

    vector_impl_free(&positions);
    vector_impl_free(&texcoords);
    vector_impl_free(&normals);

    *out_raw = raw;
    if (out_mtllib)
        *out_mtllib = mtllib;
    else
        free(mtllib);

    return true;
}

static void mdl_vao_setup_model_vertex(void)
{
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE,
        (GLsizei)sizeof(model_vertex_t),
        (void *)offsetof(model_vertex_t, px));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE,
        (GLsizei)sizeof(model_vertex_t),
        (void *)offsetof(model_vertex_t, nx));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2, 2, GL_FLOAT, GL_FALSE,
        (GLsizei)sizeof(model_vertex_t),
        (void *)offsetof(model_vertex_t, u));

}


typedef struct mdl_mtl_entry_t
{
    char *name;
    ihandle_t handle;
} mdl_mtl_entry_t;

static const char *mdl_next_token(char **p)
{
    char *s = *p;
    while (*s == ' ' || *s == '\t')
        s++;
    if (!*s)
    {
        *p = s;
        return NULL;
    }
    char *start = s;
    while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n')
        s++;
    if (*s)
    {
        *s = 0;
        s++;
    }
    *p = s;
    return start;
}

static bool mdl_load_mtl_and_submit_all(asset_manager_t *am, const char *mtl_path, vector_t *out_entries)
{
    FILE *f = fopen(mtl_path, "rb");
    if (!f)
        return false;

    vector_t entries = vector_impl_create_vector(sizeof(mdl_mtl_entry_t));

    asset_material_t cur = material_make_default(0);
    int have_cur = 0;

    char line[4096];
    while (fgets(line, sizeof(line), f))
    {
        char *s = line;
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s || *s == '\r' || *s == '\n' || *s == '#')
            continue;

        char *k = s;
        while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n')
            s++;
        if (*s)
        {
            *s = 0;
            s++;
        }
        while (*s == ' ' || *s == '\t')
            s++;

        if (!strcmp(k, "newmtl"))
        {
            if (have_cur && cur.name && cur.name[0])
            {
                ihandle_t h = asset_manager_submit_raw(am, ASSET_MATERIAL, &cur);
                mdl_mtl_entry_t e;
                e.name = mdl_strdup_trim(cur.name);
                e.handle = h;
                vector_impl_push_back(&entries, &e);
            }

            if (cur.name)
                free(cur.name);
            cur = material_make_default(0);
            free(cur.name);
            cur.name = mdl_strdup_trim(s);
            have_cur = 1;
            continue;
        }

        if (!have_cur)
            continue;

        if (!strcmp(k, "Kd"))
        {
            float r, g, b;
            if (sscanf(s, "%f %f %f", &r, &g, &b) == 3)
                cur.albedo = (vec3){r, g, b};
            continue;
        }

        if (!strcmp(k, "Ke"))
        {
            float r, g, b;
            if (sscanf(s, "%f %f %f", &r, &g, &b) == 3)
                cur.emissive = (vec3){r, g, b};
            continue;
        }

        if (!strcmp(k, "Ns"))
        {
            float ns;
            if (sscanf(s, "%f", &ns) == 1)
                cur.roughness = mdl_ns_to_roughness(ns);
            continue;
        }

        if (!strcmp(k, "d"))
        {
            float d;
            if (sscanf(s, "%f", &d) == 1)
                cur.opacity = mdl_clamp01(d);
            continue;
        }

        if (!strcmp(k, "Tr"))
        {
            float tr;
            if (sscanf(s, "%f", &tr) == 1)
                cur.opacity = mdl_clamp01(1.0f - tr);
            continue;
        }

        if (!strcmp(k, "Pm"))
        {
            float pm;
            if (sscanf(s, "%f", &pm) == 1)
                cur.metallic = mdl_clamp01(pm);
            continue;
        }

        if (!strcmp(k, "Pr"))
        {
            float pr;
            if (sscanf(s, "%f", &pr) == 1)
                cur.roughness = mdl_clamp01(pr);
            continue;
        }

        if (!strcmp(k, "map_Kd"))
        {
            const char *tex = mdl_mtl_parse_tex_path(s);
            if (tex && tex[0])
                mdl_set_tex(am, &cur.albedo_tex, mtl_path, tex);
            continue;
        }

        if (!strcmp(k, "map_Ke"))
        {
            const char *tex = mdl_mtl_parse_tex_path(s);
            if (tex && tex[0])
                mdl_set_tex(am, &cur.emissive_tex, mtl_path, tex);
            continue;
        }

        if (!strcmp(k, "map_Pr") || !strcmp(k, "map_Roughness"))
        {
            const char *tex = mdl_mtl_parse_tex_path(s);
            if (tex && tex[0])
                mdl_set_tex(am, &cur.roughness_tex, mtl_path, tex);
            continue;
        }

        if (!strcmp(k, "map_Pm") || !strcmp(k, "map_Metallic"))
        {
            const char *tex = mdl_mtl_parse_tex_path(s);
            if (tex && tex[0])
                mdl_set_tex(am, &cur.metallic_tex, mtl_path, tex);
            continue;
        }

        if (!strcmp(k, "map_AO") || !strcmp(k, "map_Occlusion"))
        {
            const char *tex = mdl_mtl_parse_tex_path(s);
            if (tex && tex[0])
                mdl_set_tex(am, &cur.occlusion_tex, mtl_path, tex);
            continue;
        }

        if (!strcmp(k, "bump") || !strcmp(k, "map_bump") || !strcmp(k, "map_Bump") || !strcmp(k, "norm") || !strcmp(k, "map_Normal") || !strcmp(k, "map_Norm"))
        {
            const char *tex = mdl_mtl_parse_tex_path(s);
            if (tex && tex[0])
            {
                if (mdl_str_contains_ci(k, "norm") || mdl_str_contains_ci(tex, "nor") || mdl_str_contains_ci(tex, "normal"))
                    mdl_set_tex(am, &cur.normal_tex, mtl_path, tex);
                else
                    mdl_set_tex(am, &cur.height_tex, mtl_path, tex);
            }
            continue;
        }
    }

    fclose(f);

    if (have_cur && cur.name && cur.name[0])
    {
        ihandle_t h = asset_manager_submit_raw(am, ASSET_MATERIAL, &cur);
        mdl_mtl_entry_t e;
        e.name = mdl_strdup_trim(cur.name);
        e.handle = h;
        vector_impl_push_back(&entries, &e);
    }

    if (cur.name)
        free(cur.name);

    *out_entries = entries;
    return true;
}

static ihandle_t mdl_find_material_handle(const vector_t *entries, const char *name)
{
    if (!entries || !name || !name[0])
        return ihandle_invalid();

    for (uint32_t i = 0; i < entries->size; ++i)
    {
        mdl_mtl_entry_t *e = (mdl_mtl_entry_t *)vector_impl_at((vector_t *)entries, i);
        if (e && e->name && mdl_str_eq_ci(e->name, name))
            return e->handle;
    }

    return ihandle_invalid();
}

static void mdl_free_mtl_entries(vector_t *entries)
{
    if (!entries)
        return;
    for (uint32_t i = 0; i < entries->size; ++i)
    {
        mdl_mtl_entry_t *e = (mdl_mtl_entry_t *)vector_impl_at(entries, i);
        if (e && e->name)
            free(e->name);
    }
    vector_impl_free(entries);
}

static bool asset_model_load(asset_manager_t *am, const char *path, asset_any_t *out_asset)
{
    if (!mdl_obj_quick_verify(path))
        return false;

    model_raw_t raw;
    char *mtllib = NULL;

    if (!mdl_obj_load_to_raw_fast(path, &raw, &mtllib))
        return false;

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_MODEL;
    out_asset->state = ASSET_STATE_LOADING;
    out_asset->as.model_raw = raw;

    if (mtllib && mtllib[0])
    {
        char *dir = mdl_path_dirname_dup(path);
        out_asset->as.model_raw.mtllib_path = dir ? mdl_path_join_dup(dir, mtllib) : mdl_strdup_trim(mtllib);
        free(dir);
    }

    free(mtllib);
    return true;
}

static bool asset_model_init(asset_manager_t *am, asset_any_t *asset)
{
    if (!asset || asset->type != ASSET_MODEL)
        return false;

    vector_t mtl_entries = {0};

    asset->as.model_raw.mtllib = ihandle_invalid();

    if (asset->as.model_raw.mtllib_path && asset->as.model_raw.mtllib_path[0])
    {
        if (!mdl_load_mtl_and_submit_all(am, asset->as.model_raw.mtllib_path, &mtl_entries))
        {
            mtl_entries = vector_impl_create_vector(sizeof(mdl_mtl_entry_t));
        }

        for (uint32_t i = 0; i < asset->as.model_raw.submeshes.size; ++i)
        {
            model_cpu_submesh_t *sm = (model_cpu_submesh_t *)vector_impl_at(&asset->as.model_raw.submeshes, i);
            if (!sm)
                continue;

            sm->material = mdl_find_material_handle(&mtl_entries, sm->material_name);
        }
    }
    else
    {
        for (uint32_t i = 0; i < asset->as.model_raw.submeshes.size; ++i)
        {
            model_cpu_submesh_t *sm = (model_cpu_submesh_t *)vector_impl_at(&asset->as.model_raw.submeshes, i);
            if (!sm)
                continue;
            sm->material = ihandle_invalid();
        }
    }

    asset_model_t model = asset_model_make();

    for (uint32_t i = 0; i < asset->as.model_raw.submeshes.size; ++i)
    {
        model_cpu_submesh_t *sm = (model_cpu_submesh_t *)vector_impl_at(&asset->as.model_raw.submeshes, i);

        mesh_t gm;
        memset(&gm, 0, sizeof(gm));
        gm.material = sm ? sm->material : ihandle_invalid();
        gm.index_count = sm ? sm->index_count : 0;

        glGenVertexArrays(1, &gm.vao);
        glBindVertexArray(gm.vao);

        glGenBuffers(1, &gm.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, gm.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sm->vertex_count * sizeof(model_vertex_t)), sm->vertices, GL_STATIC_DRAW);

        glGenBuffers(1, &gm.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sm->index_count * sizeof(uint32_t)), sm->indices, GL_STATIC_DRAW);

        mdl_vao_setup_model_vertex();

        vector_impl_push_back(&model.meshes, &gm);
    }

    mdl_free_mtl_entries(&mtl_entries);

    model_raw_destroy(&asset->as.model_raw);
    asset->as.model = model;

    return true;
}

static void asset_model_cleanup(asset_manager_t *am, asset_any_t *asset)
{
    (void)am;

    if (!asset || asset->type != ASSET_MODEL)
        return;

    if (asset->state != ASSET_STATE_READY)
        model_raw_destroy(&asset->as.model_raw);

    for (uint32_t i = 0; i < asset->as.model.meshes.size; ++i)
    {
        mesh_t *m = (mesh_t *)vector_impl_at(&asset->as.model.meshes, i);
        if (m->ibo)
            glDeleteBuffers(1, &m->ibo);
        if (m->vbo)
            glDeleteBuffers(1, &m->vbo);
        if (m->vao)
            glDeleteVertexArrays(1, &m->vao);
        memset(m, 0, sizeof(*m));
    }

    asset_model_destroy_cpu_only(&asset->as.model);
}

static asset_module_desc_t asset_module_model(void)
{
    asset_module_desc_t m;
    m.type = ASSET_MODEL;
    m.name = "ASSET_MODEL";
    m.load_fn = asset_model_load;
    m.init_fn = asset_model_init;
    m.cleanup_fn = asset_model_cleanup;
    return m;
}
