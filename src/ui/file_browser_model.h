#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "../hal/sdcard.h"
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace sigurdos::ui::file_browser {

inline bool joinPath(const char* directory, const char* name, char* out, size_t out_size)
{
    if (!directory || !name || !out || out_size == 0 || name[0] == '\0' ||
        std::strchr(name, '/')) {
        return false;
    }
    const bool root = std::strcmp(directory, "/") == 0;
    const int written = root
        ? std::snprintf(out, out_size, "/%s", name)
        : std::snprintf(out, out_size, "%s/%s", directory, name);
    return written > 0 && static_cast<size_t>(written) < out_size &&
        sigurdos_sdcard_path_valid(out);
}

inline bool parentPath(const char* path, char* out, size_t out_size)
{
    if (!sigurdos_sdcard_path_valid(path) || !out || out_size < 2) return false;
    if (std::strcmp(path, "/") == 0) {
        std::snprintf(out, out_size, "/");
        return true;
    }

    const char* slash = std::strrchr(path, '/');
    if (!slash || slash == path) {
        std::snprintf(out, out_size, "/");
        return true;
    }
    const size_t length = static_cast<size_t>(slash - path);
    if (length >= out_size) return false;
    std::memcpy(out, path, length);
    out[length] = '\0';
    return true;
}

inline bool copyName(const char* name, unsigned sequence, char* out, size_t out_size)
{
    if (!name || !out || out_size == 0 || name[0] == '\0' || std::strchr(name, '/')) {
        return false;
    }

    const char* dot = std::strrchr(name, '.');
    const bool has_extension = dot && dot != name;
    const size_t stem_length = has_extension
        ? static_cast<size_t>(dot - name)
        : std::strlen(name);
    const char* extension = has_extension ? dot : "";
    const int written = sequence <= 1
        ? std::snprintf(out, out_size, "%.*s copy%s",
                        static_cast<int>(stem_length), name, extension)
        : std::snprintf(out, out_size, "%.*s copy %u%s",
                        static_cast<int>(stem_length), name, sequence, extension);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

} // namespace sigurdos::ui::file_browser
