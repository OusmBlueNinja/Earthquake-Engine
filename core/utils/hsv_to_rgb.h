#include <math.h>
#include "types/vec4.h"

static vec4 hsv_to_rgb(float h, float s, float v, float a)
{
    float r = 0, g = 0, b = 0;

    float i_f = floorf(h * 6.0f);
    int i = (int)i_f;
    float f = h * 6.0f - i_f;

    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);

    switch (i % 6)
    {
    case 0:
        r = v;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = v;
        b = p;
        break;
    case 2:
        r = p;
        g = v;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = v;
        break;
    case 4:
        r = t;
        g = p;
        b = v;
        break;
    case 5:
        r = v;
        g = p;
        b = q;
        break;
    }

    return (vec4){r, g, b, a};
}
