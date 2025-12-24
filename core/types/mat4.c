#include "mat4.h"

static inline mat4 mat4_mul_scalar(mat4 a, mat4 b)
{
    mat4 r;
    for (int c = 0; c < 4; c++)
    {
        for (int r0 = 0; r0 < 4; r0++)
        {
            r.m[c * 4 + r0] =
                a.m[0 * 4 + r0] * b.m[c * 4 + 0] +
                a.m[1 * 4 + r0] * b.m[c * 4 + 1] +
                a.m[2 * 4 + r0] * b.m[c * 4 + 2] +
                a.m[3 * 4 + r0] * b.m[c * 4 + 3];
        }
    }
    return r;
}

#if (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)) && \
    (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
#include <immintrin.h>

static inline mat4 mat4_mul_sse2(mat4 a, mat4 b)
{
    mat4 r;

    const __m128 a0 = _mm_loadu_ps(&a.m[0]);
    const __m128 a1 = _mm_loadu_ps(&a.m[4]);
    const __m128 a2 = _mm_loadu_ps(&a.m[8]);
    const __m128 a3 = _mm_loadu_ps(&a.m[12]);

    for (int c = 0; c < 4; c++)
    {
        const float *bc = &b.m[c * 4];

        const __m128 b0 = _mm_set1_ps(bc[0]);
        const __m128 b1 = _mm_set1_ps(bc[1]);
        const __m128 b2 = _mm_set1_ps(bc[2]);
        const __m128 b3 = _mm_set1_ps(bc[3]);

        __m128 col = _mm_mul_ps(a0, b0);
        col = _mm_add_ps(col, _mm_mul_ps(a1, b1));
        col = _mm_add_ps(col, _mm_mul_ps(a2, b2));
        col = _mm_add_ps(col, _mm_mul_ps(a3, b3));

        _mm_storeu_ps(&r.m[c * 4], col);
    }

    return r;
}
#endif

mat4 mat4_mul(mat4 a, mat4 b)
{
#if (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)) && \
    (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
    return mat4_mul_sse2(a, b);
#else
    return mat4_mul_scalar(a, b);
#endif
}

mat4 mat4_inverse(mat4 m)
{
    mat4 r;
    float *a = m.m;
    float *o = r.m;

    float s0 = a[0] * a[5] - a[4] * a[1];
    float s1 = a[0] * a[6] - a[4] * a[2];
    float s2 = a[0] * a[7] - a[4] * a[3];
    float s3 = a[1] * a[6] - a[5] * a[2];
    float s4 = a[1] * a[7] - a[5] * a[3];
    float s5 = a[2] * a[7] - a[6] * a[3];

    float c5 = a[10] * a[15] - a[14] * a[11];
    float c4 = a[9] * a[15] - a[13] * a[11];
    float c3 = a[9] * a[14] - a[13] * a[10];
    float c2 = a[8] * a[15] - a[12] * a[11];
    float c1 = a[8] * a[14] - a[12] * a[10];
    float c0 = a[8] * a[13] - a[12] * a[9];

    float det =
        s0 * c5 - s1 * c4 + s2 * c3 +
        s3 * c2 - s4 * c1 + s5 * c0;

    float inv = 1.0f / det;

    o[0] = (a[5] * c5 - a[6] * c4 + a[7] * c3) * inv;
    o[1] = (-a[1] * c5 + a[2] * c4 - a[3] * c3) * inv;
    o[2] = (a[13] * s5 - a[14] * s4 + a[15] * s3) * inv;
    o[3] = (-a[9] * s5 + a[10] * s4 - a[11] * s3) * inv;

    o[4] = (-a[4] * c5 + a[6] * c2 - a[7] * c1) * inv;
    o[5] = (a[0] * c5 - a[2] * c2 + a[3] * c1) * inv;
    o[6] = (-a[12] * s5 + a[14] * s2 - a[15] * s1) * inv;
    o[7] = (a[8] * s5 - a[10] * s2 + a[11] * s1) * inv;

    o[8] = (a[4] * c4 - a[5] * c2 + a[7] * c0) * inv;
    o[9] = (-a[0] * c4 + a[1] * c2 - a[3] * c0) * inv;
    o[10] = (a[12] * s4 - a[13] * s2 + a[15] * s0) * inv;
    o[11] = (-a[8] * s4 + a[9] * s2 - a[11] * s0) * inv;

    o[12] = (-a[4] * c3 + a[5] * c1 - a[6] * c0) * inv;
    o[13] = (a[0] * c3 - a[1] * c1 + a[2] * c0) * inv;
    o[14] = (-a[12] * s3 + a[13] * s1 - a[14] * s0) * inv;
    o[15] = (a[8] * s3 - a[9] * s1 + a[10] * s0) * inv;

    return r;
}
