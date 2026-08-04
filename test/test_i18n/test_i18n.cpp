// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstring>

#include <gtest/gtest.h>

#include "hal/prefs.h"
#include "i18n/i18n.h"
#include "mocks/mock_state.h"
#include "utils/utf8_util.h"

// native_test intentionally selects source files explicitly. Include the new
// implementation here just as the production Home translation unit links it;
// this keeps platformio.ini outside this focused additive wave.
#include "../../src/i18n/i18n.cpp"

namespace {

using sigurdos::i18n::Language;
using sigurdos::i18n::StringId;

class I18nTest : public testing::Test {
protected:
    void SetUp() override
    {
        sigurdos::prefs_mock_reset();
        sigurdos::NodePrefs defaults;
        defaults.set_defaults();
        ASSERT_TRUE(sigurdos::prefs_set(defaults));
        ASSERT_TRUE(sigurdos::i18n::set_language(Language::English));
    }
};

bool valid_utf8(const char* text)
{
    if (!text) return false;
    const size_t length = std::strlen(text);
    size_t position = 0;
    while (position < length) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(text + position);
        const size_t sequence = sigurdos::utf8_sequence_length(bytes[0]);
        if (sequence == 0 || sequence > length - position ||
            !sigurdos::utf8_sequence_is_valid(bytes, sequence)) {
            return false;
        }
        position += sequence;
    }
    return true;
}

TEST_F(I18nTest, EnglishIsTheDefaultAndTablesLookup)
{
    EXPECT_EQ(Language::English, sigurdos::i18n::current_language());
    EXPECT_STREQ("CHATS", TR(HomeChats));
    EXPECT_STREQ("Funk einrichten",
                 sigurdos::i18n::tr_for(Language::German, StringId::HomeSetupWarning));
    EXPECT_STREQ("Régler radio",
                 sigurdos::i18n::tr_for(Language::French, StringId::HomeSetupWarning));
    EXPECT_STREQ("Configura radio",
                 sigurdos::i18n::tr_for(Language::Spanish, StringId::HomeSetupWarning));
    EXPECT_STREQ("Radio instellen",
                 sigurdos::i18n::tr_for(Language::Dutch, StringId::HomeSetupWarning));
    EXPECT_STREQ("INSTELLINGEN",
                 sigurdos::i18n::tr_for(Language::Dutch, StringId::HomeSettings));
    EXPECT_STREQ("Configura radio",
                 sigurdos::i18n::tr_for(Language::Italian, StringId::HomeSetupWarning));
    EXPECT_STREQ("IMPOSTAZIONI",
                 sigurdos::i18n::tr_for(Language::Italian, StringId::HomeSettings));
    EXPECT_STREQ("Ajustar rádio",
                 sigurdos::i18n::tr_for(Language::Portuguese, StringId::HomeSetupWarning));
    EXPECT_STREQ("AJUSTES",
                 sigurdos::i18n::tr_for(Language::Portuguese, StringId::HomeSettings));
    EXPECT_STREQ("Lingua",
                 sigurdos::i18n::tr_for(Language::Italian, StringId::SettingsLanguage));
    EXPECT_STREQ("Idioma",
                 sigurdos::i18n::tr_for(Language::Portuguese, StringId::SettingsLanguage));
}

TEST_F(I18nTest, MissingCurrentLanguageFallsBackToEnglish)
{
    EXPECT_TRUE(sigurdos::i18n::set_language(Language::German));
    EXPECT_STREQ("Reserved English fallback", TR(ReservedFallbackExample));
    EXPECT_STREQ("Reserved English fallback",
                 sigurdos::i18n::tr_for(Language::French,
                                        StringId::ReservedFallbackExample));
}

TEST_F(I18nTest, InvalidStringIdReturnsVisiblePlaceholder)
{
    const auto invalid = static_cast<StringId>(0xFFFFu);
    EXPECT_STREQ("[missing translation]", sigurdos::i18n::tr(invalid));
    EXPECT_STREQ("[missing translation]",
                 sigurdos::i18n::tr_for(static_cast<Language>(0xFFu), invalid));
}

TEST_F(I18nTest, TruncatedTranslationPreservesUtf8Boundaries)
{
    ASSERT_TRUE(sigurdos::i18n::set_language(Language::German));
    const char* source = TR(ChatInvalidFullSave);
    ASSERT_TRUE(std::strstr(source, "ü") != nullptr);

    char copied[12];
    const size_t copied_bytes = sigurdos::i18n::copy_truncated(
        copied, sizeof(copied), StringId::ChatInvalidFullSave);

    EXPECT_EQ(copied_bytes,
              sigurdos::utf8_truncate_bytes(source, sizeof(copied) - 1));
    EXPECT_LT(copied_bytes, sizeof(copied));
    EXPECT_EQ('\0', copied[copied_bytes]);
    EXPECT_TRUE(valid_utf8(copied));
}

TEST_F(I18nTest, LanguageSwitchUpdatesImmediatelyAndPersists)
{
    ASSERT_TRUE(sigurdos::i18n::set_language(Language::Spanish));
    EXPECT_EQ(Language::Spanish, sigurdos::i18n::current_language());
    EXPECT_STREQ("Idioma", TR(SettingsLanguage));
    EXPECT_EQ(static_cast<uint8_t>(Language::Spanish),
              sigurdos::prefs_get().language);

    sigurdos::NodePrefs loaded;
    loaded.set_defaults();
    ASSERT_TRUE(sigurdos::prefs_load(loaded));
    EXPECT_EQ(static_cast<uint8_t>(Language::Spanish), loaded.language);
}

TEST_F(I18nTest, InvalidLanguageIsRejectedWithoutChangingCurrentChoice)
{
    const auto invalid = static_cast<Language>(0xFFu);
    EXPECT_FALSE(sigurdos::i18n::set_language(invalid));
    EXPECT_EQ(Language::English, sigurdos::i18n::current_language());
}

// Regression: every enum language must round-trip through the NVS load path
// unclamped. prefs.cpp clamps language >= I18N_LANGUAGE_COUNT to 0; when that
// count was hardcoded to 4, Dutch (4) silently reverted to English on boot.
TEST_F(I18nTest, EveryLanguageRoundTripsThroughTheNvsLoadPath)
{
    for (size_t i = 0; i < static_cast<size_t>(Language::Count); ++i) {
        const auto language = static_cast<Language>(i);
        ASSERT_TRUE(sigurdos::i18n::set_language(language))
            << "set_language failed for enum value " << i;
        EXPECT_EQ(static_cast<uint8_t>(language),
                  sigurdos::prefs_get().language)
            << "in-memory pref mismatch for enum value " << i;

        sigurdos::NodePrefs loaded;
        loaded.set_defaults();
        ASSERT_TRUE(sigurdos::prefs_load(loaded))
            << "prefs_load failed after setting enum value " << i;
        EXPECT_EQ(static_cast<uint8_t>(language), loaded.language)
            << "load-path clamp regression for enum value " << i;
    }
}

} // namespace
