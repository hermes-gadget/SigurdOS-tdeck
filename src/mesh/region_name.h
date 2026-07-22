#pragma once

#include <cstddef>

namespace sigurdos {
namespace mesh {

static constexpr size_t REGION_NAME_MAX_LEN = 30;

inline bool regionNameBodyChar(char c)
{
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// Region names are an optional public/private marker followed by a non-empty
// ASCII identifier. This deliberately rejects whitespace, control bytes,
// punctuation, and non-ASCII bytes at every trust boundary.
inline bool regionNameValid(const char* name)
{
    if (!name || !name[0]) return false;
    size_t length = 0;
    const char* body = name;
    if (*body == '#' || *body == '$') ++body;
    if (!*body) return false;

    for (const char* cursor = name; *cursor; ++cursor) {
        if (++length > REGION_NAME_MAX_LEN) return false;
        if (cursor == name && (*cursor == '#' || *cursor == '$')) continue;
        if (!regionNameBodyChar(*cursor)) return false;
    }
    return true;
}

} // namespace mesh
} // namespace sigurdos
