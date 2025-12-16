#include "material.h"

void material_bind(const material_t *mat)
{
    if (!mat || !mat->shader)
        return;

    shader_bind(mat->shader);
}
