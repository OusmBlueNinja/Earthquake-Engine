#pragma once

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
