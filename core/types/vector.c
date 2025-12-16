#include "vector.h"
#include "utils/logger.h"
#include <stdlib.h>
#include <string.h>

vector_t vector_impl_create_vector(uint32_t element_size)
{
    if (element_size == 0)
    {
        LOG_ERROR("Cannot create vector_t with element_size = 0");
        return (vector_t){0};
    }

    vector_t v = {0};
    v.element_size = element_size;
    return v;
}

void vector_impl_free(vector_t *v)
{
    if (!v)
        return;

    free(v->data);
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

void vector_impl_reserve(vector_t *v, uint32_t new_capacity)
{
    if (!v || new_capacity <= v->capacity)
        return;

    void *new_data = realloc(v->data, new_capacity * v->element_size);
    if (!new_data)
    {
        LOG_ERROR("Failed to allocate memory for vector reserve (new_capacity = %u)", new_capacity);
        return;
    }

    v->data = new_data;
    v->capacity = new_capacity;
}

void vector_impl_push_back(vector_t *v, const void *element)
{
    if (!v || !element)
        return;

    if (v->size == v->capacity)
    {
        uint32_t new_capacity = v->capacity ? v->capacity * 2 : 8;
        vector_impl_reserve(v, new_capacity);
        if (v->size == v->capacity) // reserve failed
            return;
    }

    memcpy((char *)v->data + v->size * v->element_size, element, v->element_size);
    v->size++;
}

void vector_impl_pop_back(vector_t *v)
{
    if (!v || v->size == 0)
    {
        LOG_ERROR("vector_impl_pop_back called on empty vector or NULL");
        return;
    }

    v->size--;
}

void vector_impl_clear(vector_t *v)
{
    if (!v)
        return;
    v->size = 0;
}

void vector_impl_shrink_to_fit(vector_t *v)
{
    if (!v || v->capacity == v->size)
        return;

    void *new_data = realloc(v->data, v->size * v->element_size);
    if (!new_data && v->size > 0)
    {
        LOG_ERROR("Failed to shrink vector to fit");
        return;
    }

    v->data = new_data;
    v->capacity = v->size;
}

void vector_impl_resize(vector_t *v, uint32_t new_size, const void *default_value)
{
    if (!v)
        return;

    if (new_size > v->capacity)
        vector_impl_reserve(v, new_size);

    if (new_size > v->size && default_value)
    {
        for (uint32_t i = v->size; i < new_size; i++)
        {
            memcpy((char *)v->data + i * v->element_size, default_value, v->element_size);
        }
    }

    v->size = new_size;
}

void *vector_impl_at(vector_t *v, uint32_t index)
{
    if (!v || index >= v->size)
    {
        LOG_ERROR("vector_impl_at: index %u out of bounds", index);
        return NULL;
    }

    return (char *)v->data + index * v->element_size;
}

void *vector_impl_front(vector_t *v)
{
    if (!v || v->size == 0)
        return NULL;
    return v->data;
}

void *vector_impl_back(vector_t *v)
{
    if (!v || v->size == 0)
        return NULL;
    return (char *)v->data + (v->size - 1) * v->element_size;
}

void vector_impl_remove_at(vector_t *v, uint32_t index)
{
    if (!v || index >= v->size)
    {
        LOG_ERROR("vector_impl_remove_at: index %u out of bounds", index);
        return;
    }

    if (index < v->size - 1)
    {
        memmove((char *)v->data + index * v->element_size,
                (char *)v->data + (index + 1) * v->element_size,
                (v->size - index - 1) * v->element_size);
    }

    v->size--;
}
