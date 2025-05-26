#pragma once
#include <atomic>
#include <unistd.h>
#include <xmmintrin.h>
#include <glog/logging.h>
#define ALLOC_ALIGNED(var, type, size, aligned_size)                           \
    {                                                                          \
        if ((var = (type*)_mm_malloc(                                          \
                 sizeof(type) * (size), (aligned_size))) == NULL) {            \
            LOG(FATAL) << "Cannot allocate " << sizeof(type) * (size) << "B."; \
        }                                                                      \
    }
#define ALLOC(var, type, size)                                                 \
    {                                                                          \
        if ((var = (type*)malloc(sizeof(type) * (size))) == NULL) {            \
            LOG(FATAL) << "Cannot allocate " << sizeof(type) * (size) << "B."; \
        }                                                                      \
    }

#define CALLOC(ptr, type, nelem)                                              \
    do {                                                                      \
        type* p = (type*)malloc((nelem) * sizeof(type));                      \
        if (p == NULL) {                                                      \
            fprintf(stderr, "malloc failed for %s:%d\n", __FILE__, __LINE__); \
            exit(1);                                                          \
        }                                                                     \
        memset(p, 0, (nelem) * sizeof(type));                                 \
        ptr = p;                                                              \
    } while (0)
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)