// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Channel name validation — shared across UI, terminal, import, and mesh
// wrapper so disallowed characters, leading/trailing dashes, and empty names
// are consistently rejected.

#ifndef SIGURDOS_CHANNEL_VALIDATION_H
#define SIGURDOS_CHANNEL_VALIDATION_H

#include <cstddef>

namespace sigurdos {
namespace mesh {

// Returns true if `name` is a valid channel name.
//
// Rules:
//   - 1–31 stored characters after trimming whitespace (an optional leading
//     '#' counts toward this limit)
//   - Letters (A-Z, a-z), digits (0-9), and hyphens only (no consecutive dashes)
//   - Leading/trailing hyphens and empty strings are rejected
//
// `reason` (if non-null) receives a short description of the first failure.
bool channel_name_valid(const char* name, const char** reason = nullptr);

// Normalize a hashtag channel into the exact stored/key-derivation form.
// The result always starts with '#', preserves body case, and is rejected
// rather than truncated when it cannot fit in the supplied destination.
bool hashtag_channel_name_normalise(const char* name, char* out,
                                    size_t out_size,
                                    const char** reason = nullptr);

// Sanitise `name` in-place for storage:
//   - Trims leading/trailing whitespace
//   - Strips optional leading '#'
//   - Converts to lowercase for case-insensitive matching
// Returns the effective length, or 0 if empty after sanitisation.
size_t channel_name_sanitise(char* name, size_t max_len);

}  // namespace mesh
}  // namespace sigurdos

#endif
