#include "vector.h"
#include "utils/logger.h"
#include <stdlib.h>
#include <string.h>

_Noreturn static void vector_panic(const char *msg)
{
    LOG_ERROR("%s", msg);
    abort();
}

vector_t vector_impl_create_vector(uint32_t element_size)
{
    if (!element_size)
        vector_panic("vector: element_size=0");

    vector_t v = {0};
    v.element_size = element_size;
    return v;
}

void vector_impl_free(vector_t *v)
{
    if (!v)
        vector_panic("vector: free(NULL)");

    free(v->data);
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

void vector_impl_reserve(vector_t *v, uint32_t new_capacity)
{
    if (!v)
        vector_panic("vector: reserve(NULL)");
    if (new_capacity <= v->capacity)
        return;

    void *p = realloc(v->data, (size_t)new_capacity * (size_t)v->element_size);
    if (!p)
        vector_panic("vector: OOM");

    v->data = p;
    v->capacity = new_capacity;
}

void vector_impl_push_back(vector_t *v, const void *element)
{
    if (!v || !element)
        vector_panic("vector: push_back(NULL)");

    if (v->size == v->capacity)
    {
        uint32_t nc = v->capacity ? (v->capacity * 2) : 8;
        vector_impl_reserve(v, nc);
    }

    memcpy((char *)v->data + (size_t)v->size * (size_t)v->element_size, element, v->element_size);
    v->size++;
}

void vector_impl_pop_back(vector_t *v)
{
    if (!v || !v->size)
        vector_panic("vector: pop_back");

    v->size--;
}

void vector_impl_clear(vector_t *v)
{
    if (!v)
        vector_panic("vector: clear(NULL)");
    v->size = 0;
}

void vector_impl_shrink_to_fit(vector_t *v)
{
    if (!v)
        vector_panic("vector: shrink(NULL)");
    if (v->capacity == v->size)
        return;

    void *p = realloc(v->data, (size_t)v->size * (size_t)v->element_size);
    if (!p && v->size)
        vector_panic("vector: OOM");

    v->data = p;
    v->capacity = v->size;
}

void vector_impl_resize(vector_t *v, uint32_t new_size, const void *default_value)
{
    if (!v)
        vector_panic("vector: resize(NULL)");

    if (new_size > v->capacity)
        vector_impl_reserve(v, new_size);

    if (new_size > v->size && default_value)
    {
        for (uint32_t i = v->size; i < new_size; i++)
            memcpy((char *)v->data + (size_t)i * (size_t)v->element_size, default_value, v->element_size);
    }

    v->size = new_size;
}

void *vector_impl_at(vector_t *v, uint32_t index)
{
    if (!v || index >= v->size)
        vector_panic("vector: at OOB");

    return (char *)v->data + (size_t)index * (size_t)v->element_size;
}

void *vector_impl_front(vector_t *v)
{
    if (!v || !v->size)
        vector_panic("vector: front");

    return v->data;
}

void *vector_impl_back(vector_t *v)
{
    if (!v || !v->size)
        vector_panic("vector: back");

    return (char *)v->data + (size_t)(v->size - 1) * (size_t)v->element_size;
}

void vector_impl_remove_at(vector_t *v, uint32_t index)
{
    if (!v || index >= v->size)
        vector_panic("vector: remove OOB");

    if (index < v->size - 1)
    {
        memmove((char *)v->data + (size_t)index * (size_t)v->element_size,
                (char *)v->data + (size_t)(index + 1) * (size_t)v->element_size,
                (size_t)(v->size - index - 1) * (size_t)v->element_size);
    }

    v->size--;
}
