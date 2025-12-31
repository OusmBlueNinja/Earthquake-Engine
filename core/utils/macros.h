#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define STR1(x) #x
#define STR(x) STR1(x)

#define CAT1(a, b) a##b
#define CAT(a, b) CAT1(a, b)

#define CAT3(a, b, c) CAT(a, CAT(b, c))
#define CAT4(a, b, c, d) CAT(a, CAT3(b, c, d))

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) (MAX((lo), MIN((x), (hi))))

#define BIT(x) (1u << (x))

#define KB(x) ((size_t)(x) * 1024u)
#define MB(x) (KB(x) * 1024u)
#define GB(x) (MB(x) * 1024u)

#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

#define SWAP(type, a, b) \
    do                   \
    {                    \
        type _t = (a);   \
        (a) = (b);       \
        (b) = _t;        \
    } while (0)

#define UNUSED(x) (void)(x)

#define DEFER(begin, end) for (int CAT(_i_, __LINE__) = ((begin), 0); !CAT(_i_, __LINE__); CAT(_i_, __LINE__) += 1, (end))

#define STATIC_ASSERT(cond, name) typedef char CAT(static_assert_, name)[(cond) ? 1 : -1]

#if defined(_MSC_VER)
#include <intrin.h>
#define DBG_BREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define DBG_BREAK() __builtin_trap()
#else
#define DBG_BREAK() abort()
#endif

#if !defined(ASSERT_ENABLED)
#if defined(_DEBUG)
#define ASSERT_ENABLED 1
#else
#define ASSERT_ENABLED 0
#endif
#endif


static inline void assert_fail_impl(const char *expr, const char *file, int line, const char *func, const char *fmt, ...)
{
    fprintf(stderr, "\nASSERT FAILED\n  expr: %s\n  at:   %s:%d\n  fn:   %s\n", expr, file, line, func);
    if (fmt && fmt[0])
    {
        fprintf(stderr, "  msg:  ");
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fprintf(stderr, "\n");
    }
    fflush(stderr);
    DBG_BREAK();
    abort();
}

#if ASSERT_ENABLED
#define ASSERT(x) do { if (!(x)) assert_fail_impl(#x, __FILE__, (int)__LINE__, __func__, NULL); } while (0)
#define ASSERTF(x, fmt, ...) do { if (!(x)) assert_fail_impl(#x, __FILE__, (int)__LINE__, __func__, (fmt), ##__VA_ARGS__); } while (0)
#else
#define ASSERT(x) do { (void)sizeof(x); } while (0)
#define ASSERTF(x, fmt, ...) do { (void)sizeof(x); (void)sizeof(fmt); } while (0)
#endif

#define REQUIRE(x) do { if (!(x)) assert_fail_impl(#x, __FILE__, (int)__LINE__, __func__, NULL); } while (0)
#define REQUIRE_MSG(x, msg_lit) do { if (!(x)) assert_fail_impl(#x, __FILE__, (int)__LINE__, __func__, "%s", (msg_lit)); } while (0)

#define PANIC(msg_lit) do { assert_fail_impl("PANIC", __FILE__, (int)__LINE__, __func__, "%s", (msg_lit)); } while (0)
#define PANICF(fmt, ...) do { assert_fail_impl("PANIC", __FILE__, (int)__LINE__, __func__, (fmt), ##__VA_ARGS__); } while (0)
