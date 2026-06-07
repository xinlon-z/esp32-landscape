#pragma once

#include <stdlib.h>

#define MALLOC_CAP_SPIRAM 0x01
#define MALLOC_CAP_8BIT 0x02

static inline void* heap_caps_malloc(size_t size, int)
{
    return malloc(size);
}

static inline void heap_caps_free(void* ptr)
{
    free(ptr);
}
