#ifndef EMJI_DATA_H
#define EMJI_DATA_H

#include <cstdint>

// Discord-style emoji short name + UTF-8 lookup
// Sorted alphabetically by short_name for binary search

struct EmojiEntry {
    const char* short_name;  // e.g. "smile"
    const char* utf8;        // UTF-8 emoji string
};

static constexpr int EMOJI_DATA_COUNT = 343;

extern const EmojiEntry emoji_data[];

// Binary search: find emoji by short name prefix. Returns count of matches.
// Results written to out[] up to max_results. Returns number of matches found.
int emoji_search(const char* prefix, EmojiEntry* out, int max_results);

#endif  // EMJI_DATA_H
