#pragma once

#include <cstddef>

namespace sigurdos {
namespace mesh {

inline bool outboundQueueAccepted(int before, int after)
{
    return before >= 0 && after == before + 1;
}

inline bool receiveBufferFits(const void* output, int capacity,
                              size_t required)
{
    if (capacity < 0) return false;
    return required == 0 ||
        (output != nullptr && static_cast<size_t>(capacity) >= required);
}

} // namespace mesh
} // namespace sigurdos
