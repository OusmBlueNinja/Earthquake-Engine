#pragma once

typedef struct vec2i
{
    union
    {
        struct
        {
            int x;
            int y;
        };
        int v[2];
    };
} vec2i;

static inline vec2i vec2i_add(vec2i a, vec2i b)
{
    return (vec2i){a.x + b.x, a.y + b.y};
}

static inline vec2i vec2i_mult(vec2i a, vec2i b)
{
    return (vec2i){a.x * b.x, a.y * b.y};
}

static inline vec2i vec2i_div(vec2i a, vec2i b)
{
    return (vec2i){a.x / b.x, a.y / b.y};
}