#include "model.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

static void mesh_destroy(mesh_t *m)
{
    if (!m)
        return;
    if (m->ibo)
        glDeleteBuffers(1, &m->ibo);
    if (m->vbo)
        glDeleteBuffers(1, &m->vbo);
    if (m->vao)
        glDeleteVertexArrays(1, &m->vao);
    free(m);
}

static mesh_t *mesh_create_cube(void)
{
    static const float v[] = {
        -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
        -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,

        0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f,
        -0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,
        0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f,

        -0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        -0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
        -0.5f, 0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,

        0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
        0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,

        -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        -0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,

        -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
        0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,
        -0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f};

    static const uint32_t idx[] = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        8, 9, 10, 10, 11, 8,
        12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,
        20, 21, 22, 22, 23, 20};

    mesh_t *m = (mesh_t *)calloc(1, sizeof(mesh_t));
    if (!m)
        return NULL;

    glGenVertexArrays(1, &m->vao);
    glBindVertexArray(m->vao);

    glGenBuffers(1, &m->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);

    glGenBuffers(1, &m->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(6 * sizeof(float)));

    glBindVertexArray(0);

    m->index_count = (uint32_t)(sizeof(idx) / sizeof(idx[0]));
    return m;
}

static mesh_t *mesh_create_sphere(uint32_t slices, uint32_t stacks)
{
    if (slices < 3)
        slices = 3;
    if (stacks < 2)
        stacks = 2;

    uint32_t vert_w = slices + 1;
    uint32_t vert_h = stacks + 1;
    uint32_t vert_count = vert_w * vert_h;

    uint32_t quad_count = slices * stacks;
    uint32_t index_count = quad_count * 6;

    float *v = (float *)malloc((size_t)vert_count * 8u * sizeof(float));
    uint32_t *idx = (uint32_t *)malloc((size_t)index_count * sizeof(uint32_t));
    if (!v || !idx)
    {
        free(v);
        free(idx);
        return NULL;
    }

    const float pi = 3.14159265358979323846f;
    uint32_t vi = 0;

    for (uint32_t y = 0; y <= stacks; ++y)
    {
        float vty = (float)y / (float)stacks;
        float phi = vty * pi;
        float sp = sinf(phi);
        float cp = cosf(phi);

        for (uint32_t x = 0; x <= slices; ++x)
        {
            float utx = (float)x / (float)slices;
            float theta = utx * (2.0f * pi);
            float st = sinf(theta);
            float ct = cosf(theta);

            float px = ct * sp;
            float py = cp;
            float pz = st * sp;

            v[vi + 0] = px * 0.5f;
            v[vi + 1] = py * 0.5f;
            v[vi + 2] = pz * 0.5f;

            v[vi + 3] = px;
            v[vi + 4] = py;
            v[vi + 5] = pz;

            v[vi + 6] = utx;
            v[vi + 7] = 1.0f - vty;

            vi += 8;
        }
    }

    uint32_t ii = 0;
    for (uint32_t y = 0; y < stacks; ++y)
    {
        for (uint32_t x = 0; x < slices; ++x)
        {
            uint32_t i0 = y * vert_w + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + vert_w;
            uint32_t i3 = i2 + 1;

            idx[ii++] = i0;
            idx[ii++] = i2;
            idx[ii++] = i1;

            idx[ii++] = i1;
            idx[ii++] = i2;
            idx[ii++] = i3;
        }
    }

    mesh_t *m = (mesh_t *)calloc(1, sizeof(mesh_t));
    if (!m)
    {
        free(v);
        free(idx);
        return NULL;
    }

    glGenVertexArrays(1, &m->vao);
    glBindVertexArray(m->vao);

    glGenBuffers(1, &m->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vert_count * 8u * sizeof(float)), v, GL_STATIC_DRAW);

    glGenBuffers(1, &m->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)index_count * sizeof(uint32_t)), idx, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(6 * sizeof(float)));

    glBindVertexArray(0);

    m->index_count = index_count;

    free(v);
    free(idx);

    return m;
}

bool model_factory_init(model_factory_t *mf)
{
    if (!mf)
        return false;
    memset(mf, 0, sizeof(*mf));

    mf->cube = mesh_create_cube();
    if (!mf->cube)
        return false;

    mf->sphere = mesh_create_sphere(32, 16);
    if (!mf->sphere)
    {
        mesh_destroy(mf->cube);
        memset(mf, 0, sizeof(*mf));
        return false;
    }

    return true;
}

void model_factory_shutdown(model_factory_t *mf)
{
    if (!mf)
        return;
    mesh_destroy(mf->cube);
    mesh_destroy(mf->sphere);
    memset(mf, 0, sizeof(*mf));
}

mesh_t *model_factory_get_mesh(model_factory_t *mf, primitive_type_t prim)
{
    if (!mf)
        return NULL;

    switch (prim)
    {
    case PRIM_CUBE:
        return mf->cube;
    case PRIM_SPHERE:
        return mf->sphere;
    default:
        return NULL;
    }
}

model_t model_make(mesh_t *mesh, material_t *material)
{
    model_t m;
    m.mesh = mesh;
    m.material = material;
    return m;
}

model_t model_make_primitive(model_factory_t *mf, primitive_type_t prim, material_t *material)
{
    return model_make(model_factory_get_mesh(mf, prim), material);
}

void model_draw(const model_t *model)
{
    if (!model || !model->mesh)
        return;

    glBindVertexArray(model->mesh->vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)model->mesh->index_count, GL_UNSIGNED_INT, (void *)0);
    glBindVertexArray(0);
}
