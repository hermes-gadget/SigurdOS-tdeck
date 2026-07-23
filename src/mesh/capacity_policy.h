#pragma once

#include <cstddef>

namespace sigurdos {
namespace mesh {

inline bool positiveOutputCapacity(int capacity, size_t& converted)
{
    if (capacity <= 0) {
        converted = 0;
        return false;
    }
    converted = static_cast<size_t>(capacity);
    return true;
}

} // namespace mesh
} // namespace sigurdos
