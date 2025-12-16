#pragma once
#include <stdint.h>

typedef struct vector_t
{
    void *data;
    uint32_t size;
    uint32_t capacity;
    uint32_t element_size;
} vector_t;

vector_t vector_impl_create_vector(uint32_t element_size);
void vector_impl_free(vector_t *v);
void vector_impl_reserve(vector_t *v, uint32_t new_capacity);
void vector_impl_push_back(vector_t *v, const void *element);
void vector_impl_pop_back(vector_t *v);
void vector_impl_clear(vector_t *v);
void vector_impl_shrink_to_fit(vector_t *v);
void vector_impl_resize(vector_t *v, uint32_t new_size, const void *default_value);
void *vector_impl_at(vector_t *v, uint32_t index);
void *vector_impl_front(vector_t *v);
void *vector_impl_back(vector_t *v);
void vector_impl_remove_at(vector_t *v, uint32_t index);

#define create_vector(type) vector_impl_create_vector(sizeof(type))
#define vector_free(v) vector_impl_free(v)
#define vector_reserve(v, n) vector_impl_reserve(v, n)
#define vector_push_back(v, elem) vector_impl_push_back(v, elem)
#define vector_pop_back(v) vector_impl_pop_back(v)
#define vector_clear(v) vector_impl_clear(v)
#define vector_shrink_to_fit(v) vector_impl_shrink_to_fit(v)
#define vector_resize(v, n, val) vector_impl_resize(v, n, val)
#define vector_at(v, idx) ((void *)vector_impl_at(v, idx))
#define vector_front(v) ((void *)vector_impl_front(v))
#define vector_back(v) ((void *)vector_impl_back(v))
#define vector_remove_at(v, idx) vector_impl_remove_at(v, idx)

#define vector_at_type(v, idx, type) ((type *)vector_impl_at(v, idx))
#define vector_front_type(v, type) ((type *)vector_impl_front(v))
#define vector_back_type(v, type) ((type *)vector_impl_back(v))
#define vector_push_back_type(v, val, type) \
    do                                      \
    {                                       \
        type tmp = val;                     \
        vector_impl_push_back(v, &tmp);     \
    } while (0)
#define vector_resize_type(v, n, val, type) \
    do                                      \
    {                                       \
        type tmp = val;                     \
        vector_impl_resize(v, n, &tmp);     \
    } while (0)

#define VECTOR_FOR_EACH(v, type, it) \
    for (type *it = (type *)(v).data; it < ((type *)(v).data + (v).size); ++it)
