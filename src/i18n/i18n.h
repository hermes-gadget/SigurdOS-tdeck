#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Small, allocation-free UI translation framework for the T-Deck. The table
// lookup shape is adapted from the GPL-3.0-or-later wadamesh UI-touch i18n
// implementation; the language IDs and SigurdOS strings are maintained here.

#include <cstddef>
#include <cstdint>

namespace sigurdos::i18n {

enum class Language : uint8_t {
    English = 0,
    German = 1,
    French = 2,
    Spanish = 3,
    Dutch = 4,
    Italian = 5,
    Portuguese = 6,
    Count,
};

enum class StringId : uint16_t {
    HomeChats,
    HomeDms,
    HomeRooms,
    HomeContacts,
    HomeRepeaters,
    HomeAdvertise,
    HomeMap,
    HomeTerminal,
    HomePackets,
    HomeSettings,
    HomeSetup,
    HomeSignal,
    HomeSetupWarning,

    SettingsLanguage,

    ChatPublic,
    ChatNoMessagesYet,
    ChatDeleteChannelQuestion,
    ChatDeleteChannelFormat,
    ChatCancel,
    ChatDelete,
    ChatRemovalNotSaved,
    ChatTitle,
    ChatAddChannelRibbon,
    ChatSearchPlaceholder,
    ChatLoadOlder,
    ChatReturnNewest,
    ChatNoMatching,
    ChatEmoji,
    ChatClose,
    ChatFailedSuffix,
    ChatMessagePlaceholder,
    ChatSend,
    ChatAddChannelTitle,
    ChatNameLabel,
    ChatNamePlaceholder,
    ChatPskOptional,
    ChatPskPlaceholder,
    ChatAdd,
    ChatEnterChannelName,
    ChatInvalidFullSave,
    ChatMarkedAllRead,
    ChatLeaveFailed,
    ChatNamedFormat,
    ChatNamedNone,
    ChatThisChatFormat,
    ChatThisChatNone,
    ChatInvalidScope,
    ChatScopeNotSaved,
    ChatScopeClearFailed,
    ChatNamedRoutingScope,
    ChatScopeNamePlaceholder,
    ChatSet,
    ChatClear,
    ChatDmNameTooLong,
    ChatDmListFull,

    // Kept as a sparse-table example so tests and future screens can verify
    // that a missing translation falls back to English.
    ReservedFallbackExample,

    Count,
};

constexpr size_t language_count = static_cast<size_t>(Language::Count);
constexpr size_t string_count = static_cast<size_t>(StringId::Count);

bool is_valid(Language language);
const char* language_name(Language language);

// Return the current-language translation. Missing or empty entries fall back
// to English; an invalid StringId returns a visible stable placeholder.
const char* tr(StringId id);
const char* tr_for(Language language, StringId id);

// The preference is loaded lazily from NodePrefs. set_language() persists the
// selected language and updates the lookup immediately when the write succeeds.
void init();
Language current_language();
bool set_language(Language language);

// Bounded UI copies must use the shared UTF-8 helper so an accented glyph is
// never split at a buffer boundary.
size_t copy_truncated(char* dest, size_t dest_size, StringId id);

} // namespace sigurdos::i18n

#ifndef TR
#define TR(id) ::sigurdos::i18n::tr(::sigurdos::i18n::StringId::id)
#endif
