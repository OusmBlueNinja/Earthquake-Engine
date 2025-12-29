#include "vec3.h"
#include <string.h>

#if (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
#define VEC3_X86 1
#else
#define VEC3_X86 0
#endif

#if VEC3_X86 && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#define VEC3_COMPILED_WITH_SSE2 1
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#else
#define VEC3_COMPILED_WITH_SSE2 0
#endif

static int v3_has_sse2(void)
{
#if VEC3_X86
    static int inited = 0;
    static int has = 0;
    if (inited)
        return has;
    inited = 1;

#if defined(_M_X64)
    has = 1;
    return has;
#elif defined(_MSC_VER) && defined(_M_IX86)
    int info[4];
    __cpuid(info, 1);
    has = (info[3] & (1 << 26)) != 0;
    return has;
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__i386__)
    unsigned int eax = 1, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    has = (edx & (1u << 26)) != 0;
    return has;
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
    has = 1;
    return has;
#else
    has = 0;
    return has;
#endif
#else
    return 0;
#endif
}

#if VEC3_COMPILED_WITH_SSE2
static inline __m128 v3_to_m128(vec3 v)
{
    return _mm_set_ps(0.0f, v.z, v.y, v.x);
}

static inline vec3 m128_to_v3(__m128 v)
{
    float t[4];
    _mm_storeu_ps(t, v);
    return (vec3){t[0], t[1], t[2]};
}

static inline float m128_hsum3_sse2(__m128 v)
{
    __m128 shuf = _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 1, 0, 3));
    __m128 sums = _mm_add_ps(v, shuf);
    shuf = _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(1, 0, 3, 2));
    sums = _mm_add_ps(sums, shuf);
    float t[4];
    _mm_storeu_ps(t, sums);
    return t[0];
}

static inline __m128 m128_abs_sse2(__m128 v)
{
    __m128 mask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
    return _mm_and_ps(v, mask);
}

static inline __m128 m128_min_sse2(__m128 a, __m128 b)
{
    __m128 m = _mm_cmplt_ps(a, b);
    return _mm_or_ps(_mm_and_ps(m, a), _mm_andnot_ps(m, b));
}

static inline __m128 m128_max_sse2(__m128 a, __m128 b)
{
    __m128 m = _mm_cmpgt_ps(a, b);
    return _mm_or_ps(_mm_and_ps(m, a), _mm_andnot_ps(m, b));
}
#endif

vec3 vec3_make(float x, float y, float z)
{
    return (vec3){x, y, z};
}

vec3 vec3_splat(float s)
{
    return (vec3){s, s, s};
}

vec3 vec3_add(vec3 a, vec3 b)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
        return m128_to_v3(_mm_add_ps(v3_to_m128(a), v3_to_m128(b)));
#endif
    return (vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

vec3 vec3_sub(vec3 a, vec3 b)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
        return m128_to_v3(_mm_sub_ps(v3_to_m128(a), v3_to_m128(b)));
#endif
    return (vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

vec3 vec3_mul(vec3 a, vec3 b)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
        return m128_to_v3(_mm_mul_ps(v3_to_m128(a), v3_to_m128(b)));
#endif
    return (vec3){a.x * b.x, a.y * b.y, a.z * b.z};
}

vec3 vec3_div(vec3 a, vec3 b)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
        return m128_to_v3(_mm_div_ps(v3_to_m128(a), v3_to_m128(b)));
#endif
    return (vec3){a.x / b.x, a.y / b.y, a.z / b.z};
}

vec3 vec3_mul_f(vec3 a, float s)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
        return m128_to_v3(_mm_mul_ps(v3_to_m128(a), _mm_set1_ps(s)));
#endif
    return (vec3){a.x * s, a.y * s, a.z * s};
}

vec3 vec3_div_f(vec3 a, float s)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
        return m128_to_v3(_mm_div_ps(v3_to_m128(a), _mm_set1_ps(s)));
#endif
    return (vec3){a.x / s, a.y / s, a.z / s};
}

vec3 vec3_neg(vec3 v)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
        return m128_to_v3(_mm_sub_ps(_mm_setzero_ps(), v3_to_m128(v)));
#endif
    return (vec3){-v.x, -v.y, -v.z};
}

vec3 vec3_abs(vec3 v)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
        return m128_to_v3(m128_abs_sse2(v3_to_m128(v)));
#endif
    return (vec3){fabsf(v.x), fabsf(v.y), fabsf(v.z)};
}

float vec3_dot(vec3 a, vec3 b)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
        return m128_hsum3_sse2(_mm_mul_ps(v3_to_m128(a), v3_to_m128(b)));
#endif
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3 vec3_cross(vec3 a, vec3 b)
{
    return (vec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

float vec3_len_sq(vec3 v)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
        return m128_hsum3_sse2(_mm_mul_ps(v3_to_m128(v), v3_to_m128(v)));
#endif
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

float vec3_len(vec3 v)
{
    return sqrtf(vec3_len_sq(v));
}

vec3 vec3_norm(vec3 v)
{
    float d = vec3_len_sq(v);
    if (d <= 0.0f)
        return (vec3){0.0f, 0.0f, 0.0f};
    return vec3_mul_f(v, 1.0f / sqrtf(d));
}

vec3 vec3_norm_safe(vec3 v, float eps)
{
    float d = vec3_len_sq(v);
    if (d <= eps * eps)
        return (vec3){0.0f, 0.0f, 0.0f};
    return vec3_mul_f(v, 1.0f / sqrtf(d));
}

float vec3_dist_sq(vec3 a, vec3 b)
{
    vec3 d = vec3_sub(a, b);
    return vec3_len_sq(d);
}

float vec3_dist(vec3 a, vec3 b)
{
    return sqrtf(vec3_dist_sq(a, b));
}

vec3 vec3_min(vec3 a, vec3 b)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
        return m128_to_v3(m128_min_sse2(v3_to_m128(a), v3_to_m128(b)));
#endif
    return (vec3){fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z)};
}

vec3 vec3_max(vec3 a, vec3 b)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
        return m128_to_v3(m128_max_sse2(v3_to_m128(a), v3_to_m128(b)));
#endif
    return (vec3){fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z)};
}

vec3 vec3_clamp(vec3 v, vec3 lo, vec3 hi)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
    {
        __m128 mv = v3_to_m128(v);
        __m128 mlo = v3_to_m128(lo);
        __m128 mhi = v3_to_m128(hi);
        return m128_to_v3(m128_max_sse2(mlo, m128_min_sse2(mv, mhi)));
    }
#endif
    vec3 r;
    r.x = fmaxf(lo.x, fminf(v.x, hi.x));
    r.y = fmaxf(lo.y, fminf(v.y, hi.y));
    r.z = fmaxf(lo.z, fminf(v.z, hi.z));
    return r;
}

vec3 vec3_lerp(vec3 a, vec3 b, float t)
{
#if VEC3_COMPILED_WITH_SSE2
    if (v3_has_sse2())
    {
        __m128 ma = v3_to_m128(a);
        __m128 mb = v3_to_m128(b);
        __m128 mt = _mm_set1_ps(t);
        return m128_to_v3(_mm_add_ps(ma, _mm_mul_ps(_mm_sub_ps(mb, ma), mt)));
    }
#endif
    return (vec3){
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t};
}

vec3 vec3_reflect(vec3 v, vec3 n)
{
    float d = vec3_dot(v, n);
    return vec3_sub(v, vec3_mul_f(n, 2.0f * d));
}

vec3 vec3_project(vec3 v, vec3 onto)
{
    float d = vec3_dot(onto, onto);
    if (d <= 0.0f)
        return (vec3){0.0f, 0.0f, 0.0f};
    return vec3_mul_f(onto, vec3_dot(v, onto) / d);
}

vec3 vec3_reject(vec3 v, vec3 onto)
{
    return vec3_sub(v, vec3_project(v, onto));
}

int vec3_near_equal(vec3 a, vec3 b, float eps)
{
    vec3 d = vec3_abs(vec3_sub(a, b));
    return (d.x <= eps) && (d.y <= eps) && (d.z <= eps);
}
