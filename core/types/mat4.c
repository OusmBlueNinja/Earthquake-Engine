#include "mat4.h"
#include <math.h>
#include <string.h> // memcpy

// Detect SSE2 on x86/x64
#if (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)) && \
    (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
#define MAT4_USE_SSE2 1
#include <immintrin.h>
#else
#define MAT4_USE_SSE2 0
#endif

mat4 mat4_identity(void)
{
    mat4 r;

#if MAT4_USE_SSE2
    // column-major: each column is 4 floats contiguous
    const __m128 c0 = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f); // (1,0,0,0)
    const __m128 c1 = _mm_set_ps(0.0f, 0.0f, 1.0f, 0.0f); // (0,1,0,0)
    const __m128 c2 = _mm_set_ps(0.0f, 1.0f, 0.0f, 0.0f); // (0,0,1,0)
    const __m128 c3 = _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f); // (0,0,0,1)

    _mm_storeu_ps(&r.m[0], c0);
    _mm_storeu_ps(&r.m[4], c1);
    _mm_storeu_ps(&r.m[8], c2);
    _mm_storeu_ps(&r.m[12], c3);
#else
    r.m[0] = 1;
    r.m[1] = 0;
    r.m[2] = 0;
    r.m[3] = 0;
    r.m[4] = 0;
    r.m[5] = 1;
    r.m[6] = 0;
    r.m[7] = 0;
    r.m[8] = 0;
    r.m[9] = 0;
    r.m[10] = 1;
    r.m[11] = 0;
    r.m[12] = 0;
    r.m[13] = 0;
    r.m[14] = 0;
    r.m[15] = 1;
#endif
    return r;
}

mat4 mat4_translate(vec3 v)
{
    mat4 r = mat4_identity();
    r.m[12] = v.x;
    r.m[13] = v.y;
    r.m[14] = v.z;
    return r;
}

mat4 mat4_scale(vec3 v)
{
#if MAT4_USE_SSE2
    mat4 r;
    const __m128 c0 = _mm_set_ps(0.0f, 0.0f, 0.0f, v.x);  // (vx,0,0,0)
    const __m128 c1 = _mm_set_ps(0.0f, 0.0f, v.y, 0.0f);  // (0,vy,0,0)
    const __m128 c2 = _mm_set_ps(0.0f, v.z, 0.0f, 0.0f);  // (0,0,vz,0)
    const __m128 c3 = _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f); // (0,0,0,1)
    _mm_storeu_ps(&r.m[0], c0);
    _mm_storeu_ps(&r.m[4], c1);
    _mm_storeu_ps(&r.m[8], c2);
    _mm_storeu_ps(&r.m[12], c3);
    return r;
#else
    mat4 r = mat4_identity();
    r.m[0] = v.x;
    r.m[5] = v.y;
    r.m[10] = v.z;
    return r;
#endif
}

mat4 mat4_rotate_x(float a)
{
    float c = cosf(a), s = sinf(a);
    mat4 r = mat4_identity();
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

mat4 mat4_rotate_y(float a)
{
    float c = cosf(a), s = sinf(a);
    mat4 r = mat4_identity();
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
}

mat4 mat4_rotate_z(float a)
{
    float c = cosf(a), s = sinf(a);
    mat4 r = mat4_identity();
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
}

mat4 mat4_perspective(float fov, float aspect, float zn, float zf)
{
    float t = tanf(fov * 0.5f);
    mat4 r = (mat4){0};
    r.m[0] = 1.0f / (aspect * t);
    r.m[5] = 1.0f / t;
    r.m[10] = -(zf + zn) / (zf - zn);
    r.m[11] = -1.0f;
    r.m[14] = -(2.0f * zf * zn) / (zf - zn);
    return r;
}

mat4 mat4_ortho(float l, float r0, float b, float t, float zn, float zf)
{
    mat4 r = mat4_identity();
    r.m[0] = 2.0f / (r0 - l);
    r.m[5] = 2.0f / (t - b);
    r.m[10] = -2.0f / (zf - zn);
    r.m[12] = -(r0 + l) / (r0 - l);
    r.m[13] = -(t + b) / (t - b);
    r.m[14] = -(zf + zn) / (zf - zn);
    return r;
}

mat4 mat4_lookat(vec3 eye, vec3 at, vec3 up)
{
    vec3 f = vec3_norm((vec3){at.x - eye.x, at.y - eye.y, at.z - eye.z});
    vec3 s = vec3_norm(vec3_cross(f, up));
    vec3 u = vec3_cross(s, f);

    mat4 r = mat4_identity();
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;
    r.m[2] = -f.x;
    r.m[6] = -f.y;
    r.m[10] = -f.z;

    r.m[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
    r.m[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
    r.m[14] = (f.x * eye.x + f.y * eye.y + f.z * eye.z);
    return r;
}

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

#if MAT4_USE_SSE2
static inline mat4 mat4_mul_sse2(mat4 a, mat4 b)
{
    // column-major; each column is contiguous
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
#if MAT4_USE_SSE2
    return mat4_mul_sse2(a, b);
#else
    return mat4_mul_scalar(a, b);
#endif
}

static inline mat4 mat4_inverse_scalar(mat4 m)
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

    // avoid inf/nan explosion on near-singular matrices
    if (fabsf(det) < 1e-8f)
        return (mat4){0};

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

#if MAT4_USE_SSE2
static inline float m128_lane(__m128 v, int lane)
{
    switch (lane & 3)
    {
    default:
    case 0:
        return _mm_cvtss_f32(v);
    case 1:
        return _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1)));
    case 2:
        return _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2)));
    case 3:
        return _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 3, 3, 3)));
    }
}

static inline void m128_swap(__m128 *a, __m128 *b)
{
    __m128 t = *a;
    *a = *b;
    *b = t;
}

static inline mat4 mat4_inverse_sse2(mat4 m)
{
    // Load column-major, transpose to get row-major rows in registers
    __m128 c0 = _mm_loadu_ps(&m.m[0]);
    __m128 c1 = _mm_loadu_ps(&m.m[4]);
    __m128 c2 = _mm_loadu_ps(&m.m[8]);
    __m128 c3 = _mm_loadu_ps(&m.m[12]);
    _MM_TRANSPOSE4_PS(c0, c1, c2, c3); // now c0..c3 are rows r0..r3

    __m128 row0 = c0, row1 = c1, row2 = c2, row3 = c3;

    // Inverse accumulator = identity (row-major)
    __m128 inv0 = _mm_set_ps(0, 0, 0, 1);
    __m128 inv1 = _mm_set_ps(0, 0, 1, 0);
    __m128 inv2 = _mm_set_ps(0, 1, 0, 0);
    __m128 inv3 = _mm_set_ps(1, 0, 0, 0);

    __m128 rows[4] = {row0, row1, row2, row3};
    __m128 invs[4] = {inv0, inv1, inv2, inv3};

    // Gauss–Jordan with partial pivoting
    for (int i = 0; i < 4; i++)
    {
        // pivot selection
        int p = i;
        float maxabs = fabsf(m128_lane(rows[i], i));
        for (int j = i + 1; j < 4; j++)
        {
            float v = fabsf(m128_lane(rows[j], i));
            if (v > maxabs)
            {
                maxabs = v;
                p = j;
            }
        }

        if (maxabs < 1e-8f)
            return (mat4){0}; // singular / near-singular

        if (p != i)
        {
            m128_swap(&rows[i], &rows[p]);
            m128_swap(&invs[i], &invs[p]);
        }

        // scale pivot row to make pivot = 1
        float pivot = m128_lane(rows[i], i);
        float inv_pivot = 1.0f / pivot;
        __m128 s = _mm_set1_ps(inv_pivot);
        rows[i] = _mm_mul_ps(rows[i], s);
        invs[i] = _mm_mul_ps(invs[i], s);

        // eliminate column i in other rows
        for (int j = 0; j < 4; j++)
        {
            if (j == i)
                continue;

            float factor = m128_lane(rows[j], i);
            __m128 f = _mm_set1_ps(factor);

            rows[j] = _mm_sub_ps(rows[j], _mm_mul_ps(f, rows[i]));
            invs[j] = _mm_sub_ps(invs[j], _mm_mul_ps(f, invs[i]));
        }
    }

    // invs[] are inverse rows (row-major). Transpose back to column-major storage.
    __m128 r0 = invs[0], r1 = invs[1], r2 = invs[2], r3 = invs[3];
    _MM_TRANSPOSE4_PS(r0, r1, r2, r3); // now r0..r3 are columns

    mat4 out;
    _mm_storeu_ps(&out.m[0], r0);
    _mm_storeu_ps(&out.m[4], r1);
    _mm_storeu_ps(&out.m[8], r2);
    _mm_storeu_ps(&out.m[12], r3);
    return out;
}
#endif

mat4 mat4_inverse(mat4 m)
{
#if MAT4_USE_SSE2
    return mat4_inverse_sse2(m);
#else
    return mat4_inverse_scalar(m);
#endif
}
vec4 mat4_mul_vec4(mat4 m, vec4 v)
{
#if MAT4_USE_SSE2
    __m128 c0 = _mm_loadu_ps(&m.m[0]);
    __m128 c1 = _mm_loadu_ps(&m.m[4]);
    __m128 c2 = _mm_loadu_ps(&m.m[8]);
    __m128 c3 = _mm_loadu_ps(&m.m[12]);

    __m128 vx = _mm_set1_ps(v.x);
    __m128 vy = _mm_set1_ps(v.y);
    __m128 vz = _mm_set1_ps(v.z);
    __m128 vw = _mm_set1_ps(v.w);

    __m128 r = _mm_mul_ps(c0, vx);
    r = _mm_add_ps(r, _mm_mul_ps(c1, vy));
    r = _mm_add_ps(r, _mm_mul_ps(c2, vz));
    r = _mm_add_ps(r, _mm_mul_ps(c3, vw));

    vec4 out;
    _mm_storeu_ps((float *)&out, r);
    return out;
#else
    vec4 r;
    r.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w;
    r.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w;
    r.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w;
    r.w = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w;
    return r;
#endif
}
