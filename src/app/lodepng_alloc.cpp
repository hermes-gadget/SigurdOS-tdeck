#include <cstddef>
#include <cstdlib>
#include <esp_heap_caps.h>

void* lodepng_malloc(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}

void* lodepng_realloc(void* ptr, size_t new_size) {
    void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}

void lodepng_free(void* ptr) {
    heap_caps_free(ptr);
}
