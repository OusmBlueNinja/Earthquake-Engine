#pragma once

typedef struct vec2
{
    union
    {
        struct
        {
            float x;
            float y;
        };
        float v[2];
    };
} vec2;

static inline vec2 vec2_add(vec2 a, vec2 b)
{
    return (vec2){ a.x + b.x, a.y + b.y };
}

static inline vec2 vec2_mult(vec2 a, vec2 b)
{
    return (vec2){ a.x * b.x, a.y * b.y };
}

static inline vec2 vec2_div(vec2 a, vec2 b)
{
    return (vec2){ a.x / b.x, a.y / b.y };
}