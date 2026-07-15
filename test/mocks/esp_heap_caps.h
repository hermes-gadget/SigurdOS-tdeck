#pragma once

#include <cstdlib>

#define MALLOC_CAP_SPIRAM   0x01
#define MALLOC_CAP_INTERNAL 0x02
#define MALLOC_CAP_8BIT     0x04

// Tests can force the next N SPIRAM allocations to fail so DRAM-fallback
// paths (e.g. ChatMessageBuffer, #542) are exercisable natively.
inline int& heap_caps_mock_fail_spiram()
{
    static int fail_count = 0;
    return fail_count;
}

inline void* heap_caps_malloc(size_t size, int caps)
{
    if ((caps & MALLOC_CAP_SPIRAM) && heap_caps_mock_fail_spiram() > 0) {
        heap_caps_mock_fail_spiram()--;
        return nullptr;
    }
    return std::malloc(size);
}

inline void heap_caps_free(void* ptr)
{
    std::free(ptr);
}
