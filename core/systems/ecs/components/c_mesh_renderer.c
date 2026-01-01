#include "c_mesh_renderer.h"

#include "ecs/component.h"

static void c_mesh_renderer_ctor(void *component)
{
    c_mesh_renderer_t *m = (c_mesh_renderer_t *)component;
    m->model = ihandle_invalid();
}

void c_mesh_renderer_register(ecs_world_t *w)
{
    ecs_register_component_ctor(w, c_mesh_renderer_t, c_mesh_renderer_ctor);
}
