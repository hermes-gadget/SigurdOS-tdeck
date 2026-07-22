#pragma once

#include <cstdint>

namespace sigurdos {
namespace mesh {

// MeshCore companion radios repeat every eligible packet whenever the setting
// is non-zero. Region membership is not an additional forwarding gate.
inline bool clientRepeatAllowsForward(uint8_t client_repeat)
{
    return client_repeat != 0;
}

} // namespace mesh
} // namespace sigurdos
