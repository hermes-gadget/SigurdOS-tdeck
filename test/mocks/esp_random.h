#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void esp_fill_random(void* buffer, size_t length);

#ifdef __cplusplus
}
#endif
