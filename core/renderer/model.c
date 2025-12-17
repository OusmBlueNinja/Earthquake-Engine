#include "model.h"
#include <GL/glew.h>

void model_draw(const model_t *model)
{
    if (!model || !model->mesh)
        return;

    glBindVertexArray(model->mesh->vao);
    glDrawElements(GL_TRIANGLES, model->mesh->index_count, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}
