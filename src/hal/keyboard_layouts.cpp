// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include "keyboard_layouts.h"

#include "prefs.h"

// Physical-key phonetic tables adapted from wadamesh KeyboardLayouts.cpp at
// commit 1e00ac3876a0604557304ea5ad5849c6b5ee5d36 (GPL-3.0-or-later), with
// the same 12-layout ordering used by issue #752.

namespace sigurdos::keyboard_layouts {
namespace {

using Table26 = const char* [26];
using Table10 = const char* [10];

static const Table26 bg_lower = {
    "а", "б", "ц", "д", "е", "ф", "г", "х", "и", "й", "к", "л", "м",
    "н", "о", "п", "я", "р", "с", "т", "у", "ж", "в", "ь", "ъ", "з",
};
static const Table26 bg_upper = {
    "А", "Б", "Ц", "Д", "Е", "Ф", "Г", "Х", "И", "Й", "К", "Л", "М",
    "Н", "О", "П", "Я", "Р", "С", "Т", "У", "Ж", "В", "Ь", "Ъ", "З",
};
static const Table10 bg_digits = {
    nullptr, "ш", "щ", "ч", "ю", nullptr, nullptr, nullptr, nullptr, nullptr,
};
static const Table10 bg_digits_shift = {
    nullptr, "Ш", "Щ", "Ч", "Ю", nullptr, nullptr, nullptr, nullptr, nullptr,
};

static const Table26 ru_lower = {
    "а", "б", "ц", "д", "е", "ф", "г", "х", "и", "й", "к", "л", "м",
    "н", "о", "п", "я", "р", "с", "т", "у", "в", "ш", "ж", "ы", "з",
};
static const Table26 ru_upper = {
    "А", "Б", "Ц", "Д", "Е", "Ф", "Г", "Х", "И", "Й", "К", "Л", "М",
    "Н", "О", "П", "Я", "Р", "С", "Т", "У", "В", "Ш", "Ж", "Ы", "З",
};
static const Table10 ru_digits = {
    nullptr, "ч", "щ", "ъ", "ь", "э", "ю", "ё", nullptr, nullptr,
};
static const Table10 ru_digits_shift = {
    nullptr, "Ч", "Щ", "Ъ", "Ь", "Э", "Ю", "Ё", nullptr, nullptr,
};

static const Table26 uk_lower = {
    "а", "б", "ц", "д", "е", "ф", "ґ", "г", "і", "й", "к", "л", "м",
    "н", "о", "п", "я", "р", "с", "т", "у", "в", "в", "х", "и", "з",
};
static const Table26 uk_upper = {
    "А", "Б", "Ц", "Д", "Е", "Ф", "Ґ", "Г", "І", "Й", "К", "Л", "М",
    "Н", "О", "П", "Я", "Р", "С", "Т", "У", "В", "В", "Х", "И", "З",
};
static const Table10 uk_digits = {
    nullptr, "ж", "ч", "ш", "щ", "є", "ї", "ь", "ю", nullptr,
};
static const Table10 uk_digits_shift = {
    nullptr, "Ж", "Ч", "Ш", "Щ", "Є", "Ї", "Ь", "Ю", nullptr,
};

static const Table26 sr_lower = {
    "а", "б", "ц", "д", "е", "ф", "г", "х", "и", "ј", "к", "л", "м",
    "н", "о", "п", "љ", "р", "с", "т", "у", "в", "ш", "џ", "њ", "з",
};
static const Table26 sr_upper = {
    "А", "Б", "Ц", "Д", "Е", "Ф", "Г", "Х", "И", "Ј", "К", "Л", "М",
    "Н", "О", "П", "Љ", "Р", "С", "Т", "У", "В", "Ш", "Џ", "Њ", "З",
};
static const Table10 sr_digits = {
    nullptr, "ч", "ћ", "ђ", "ж", nullptr, nullptr, nullptr, nullptr, nullptr,
};
static const Table10 sr_digits_shift = {
    nullptr, "Ч", "Ћ", "Ђ", "Ж", nullptr, nullptr, nullptr, nullptr, nullptr,
};

static const Table26 el_lower = {
    "α", "β", "ψ", "δ", "ε", "φ", "γ", "η", "ι", "ξ", "κ", "λ", "μ",
    "ν", "ο", "π", nullptr, "ρ", "σ", "τ", "θ", "ω", "ς", "χ", "υ", "ζ",
};
static const Table26 el_upper = {
    "Α", "Β", "Ψ", "Δ", "Ε", "Φ", "Γ", "Η", "Ι", "Ξ", "Κ", "Λ", "Μ",
    "Ν", "Ο", "Π", nullptr, "Ρ", "Σ", "Τ", "Θ", "Ω", "Σ", "Χ", "Υ", "Ζ",
};

static const Table26 ar_lower = {
    "ش", "لا", "ؤ", "ي", "ث", "ب", "ل", "ا", "ه", "ت", "ن", "م", "ة",
    "ى", "خ", "ح", "ض", "ق", "س", "ف", "ع", "ر", "ص", "ء", "غ", "ئ",
};
static const Table10 ar_digits = {
    nullptr, "ج", "د", "ذ", "ز", "ط", "ظ", "و", nullptr, nullptr,
};

static const Table26 fr_lower = {
    "q", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
    "n", "o", "p", "a", "r", "s", "t", "u", "v", "z", "x", "y", "w",
};
static const Table26 fr_upper = {
    "Q", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "A", "R", "S", "T", "U", "V", "Z", "X", "Y", "W",
};

static const Table26 latin_lower = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
    "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
};
static const Table26 latin_upper = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
};
static const Table26 de_lower = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
    "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "z", "y",
};
static const Table26 de_upper = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Z", "Y",
};
static const Table10 nl_digits = {
    nullptr, "ij", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};
static const Table10 nl_digits_shift = {
    nullptr, "IJ", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};
static const Table10 es_digits = {
    nullptr, "ñ", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};
static const Table10 es_digits_shift = {
    nullptr, "Ñ", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};
static const Table10 it_digits = {
    nullptr, "à", "è", "ì", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};
static const Table10 it_digits_shift = {
    nullptr, "À", "È", "Ì", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};

struct Map {
    const char* const* lower;
    const char* const* upper;
    const char* const* digits;
    const char* const* digits_shift;
};

static const Map maps[COUNT] = {
    {nullptr, nullptr, nullptr, nullptr},
    {bg_lower, bg_upper, bg_digits, bg_digits_shift},
    {ru_lower, ru_upper, ru_digits, ru_digits_shift},
    {uk_lower, uk_upper, uk_digits, uk_digits_shift},
    {sr_lower, sr_upper, sr_digits, sr_digits_shift},
    {el_lower, el_upper, nullptr, nullptr},
    {ar_lower, ar_lower, ar_digits, ar_digits},
    {fr_lower, fr_upper, nullptr, nullptr},
    {latin_lower, latin_upper, nl_digits, nl_digits_shift},
    {de_lower, de_upper, nullptr, nullptr},
    {latin_lower, latin_upper, es_digits, es_digits_shift},
    {latin_lower, latin_upper, it_digits, it_digits_shift},
};

static const char* names[COUNT] = {
    "EN", "BG", "RU", "UK", "SR", "EL", "AR", "FR", "NL", "DE", "ES", "IT",
};

static Id current = Id::English;

static bool valid(Id id)
{
    return static_cast<uint8_t>(id) < COUNT;
}

} // namespace

bool CycleGesture::on_space(uint32_t now_ms, uintptr_t target)
{
    if (target == 0) {
        reset();
        return false;
    }

    const bool is_double = armed_ && target_ == target &&
                           now_ms - last_ms_ <= DOUBLE_SPACE_WINDOW_MS;
    if (is_double) {
        reset();
        return true;
    }

    armed_ = true;
    last_ms_ = now_ms;
    target_ = target;
    return false;
}

void CycleGesture::reset()
{
    armed_ = false;
    last_ms_ = 0;
    target_ = 0;
}

void init()
{
    const uint8_t stored = prefs_get().kbd_layout;
    current = stored < COUNT ? static_cast<Id>(stored) : Id::English;
}

Id active()
{
    return current;
}

const char* name(Id id)
{
    return valid(id) ? names[static_cast<uint8_t>(id)] : names[0];
}

bool set_active(Id id, bool persist)
{
    if (!valid(id)) return false;
    current = id;
    if (persist) {
        NodePrefs updated = prefs_get();
        updated.kbd_layout = static_cast<uint8_t>(id);
        prefs_set(updated);
    }
    return true;
}

Id cycle()
{
    const uint8_t next = (static_cast<uint8_t>(current) + 1) % COUNT;
    set_active(static_cast<Id>(next), true);
    return current;
}

const char* map_key(Id id, int key, bool shifted)
{
    if (!valid(id)) return nullptr;
    const Map& map = maps[static_cast<uint8_t>(id)];
    if (!map.lower) return nullptr;

    if (key >= 'a' && key <= 'z') return map.lower[key - 'a'];
    if (key >= 'A' && key <= 'Z') return map.upper[key - 'A'];
    if (key >= '0' && key <= '9') {
        const int digit = key - '0';
        const char* const* table = shifted ? map.digits_shift : map.digits;
        return table ? table[digit] : nullptr;
    }

    if (!shifted) return nullptr;

    static constexpr char shifted_digits[] = ")!@#$%^&*(";
    for (int digit = 0; digit < 10; digit++) {
        if (key == shifted_digits[digit]) {
            return map.digits_shift ? map.digits_shift[digit] : nullptr;
        }
    }
    return nullptr;
}

} // namespace sigurdos::keyboard_layouts
