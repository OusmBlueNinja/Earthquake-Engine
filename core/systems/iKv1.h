#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    IKV_NULL,
    IKV_STRING,
    IKV_INT,
    IKV_FLOAT,
    IKV_BOOL,
    IKV_OBJECT,
    IKV_ARRAY
} ikv_type_t;

typedef struct ikv_node_t ikv_node_t;

typedef struct
{
    ikv_type_t element_type;
    uint32_t count;
    ikv_node_t **items;
} ikv_array_t;

typedef struct
{
    uint32_t bucket_count;
    uint32_t size;
    ikv_node_t **buckets;
} ikv_object_t;

struct ikv_node_t
{
    char *key;
    ikv_type_t type;
    union
    {
        char *string;
        int64_t i;
        double f;
        bool b;
        ikv_object_t object;
        ikv_array_t array;
    } value;
    ikv_node_t *next;
};

ikv_node_t *ikv_create_object(const char *key);
ikv_node_t *ikv_create_array(const char *key, ikv_type_t element_type);
void ikv_free(ikv_node_t *node);

void ikv_object_set_int(ikv_node_t *obj, const char *key, int64_t value);
void ikv_object_set_float(ikv_node_t *obj, const char *key, double value);
void ikv_object_set_bool(ikv_node_t *obj, const char *key, bool value);
void ikv_object_set_string(ikv_node_t *obj, const char *key, const char *value);
ikv_node_t *ikv_object_add_object(ikv_node_t *obj, const char *key);
ikv_node_t *ikv_object_add_array(ikv_node_t *obj, const char *key, ikv_type_t element_type);

void ikv_array_add_int(ikv_node_t *array, int64_t value);
void ikv_array_add_float(ikv_node_t *array, double value);
void ikv_array_add_bool(ikv_node_t *array, bool value);
void ikv_array_add_string(ikv_node_t *array, const char *value);
ikv_node_t *ikv_array_add_object(ikv_node_t *array);

ikv_node_t *ikv_object_get(const ikv_node_t *obj, const char *key);
ikv_node_t *ikv_array_get(const ikv_node_t *array, uint32_t index);

const char *ikv_as_string(const ikv_node_t *node);
int64_t ikv_as_int(const ikv_node_t *node);
double ikv_as_float(const ikv_node_t *node);
bool ikv_as_bool(const ikv_node_t *node);

bool ikv_write_file(const char *path, const ikv_node_t *root);
ikv_node_t *ikv_parse_file(const char *path);
ikv_node_t *ikv_parse_string(const char *src);
