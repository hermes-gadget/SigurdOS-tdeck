// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstring>

#include <gtest/gtest.h>

#include "i18n/i18n.h"
#include "ui/text_fit.h"

// native_test source selection intentionally keeps the core implementation
// independent from LVGL and includes it directly for this focused suite.
#include "../../src/ui/text_fit.cpp"
#include "../../src/i18n/i18n.cpp"

namespace {

using sigurdos::i18n::Language;
using sigurdos::i18n::StringId;
using sigurdos::ui::TextFitFontLadder;

struct FontMetric {
    int width;
};

int measure(const char* text, const void* font, void*)
{
    const auto* metric = static_cast<const FontMetric*>(font);
    return static_cast<int>(std::strlen(text ? text : "")) * metric->width;
}

TextFitFontLadder ladder(const FontMetric* const* fonts, std::size_t count)
{
    TextFitFontLadder result;
    result.count = count;
    for (std::size_t index = 0; index < count; ++index) {
        result.fonts[index] = fonts[index];
    }
    return result;
}

TEST(TextFitCore, KeepsLargestFontWhenTextFits)
{
    const FontMetric large{6};
    const FontMetric small{4};
    const FontMetric* fonts[] = {&large, &small};

    const auto choice = sigurdos::ui::text_fit_choose_font(
        "short", 40, ladder(fonts, 2), measure);

    EXPECT_EQ(choice.index, 0u);
    EXPECT_EQ(choice.font, &large);
}

TEST(TextFitCore, DescendsOrderedLadderWhenNeeded)
{
    const FontMetric large{10};
    const FontMetric medium{7};
    const FontMetric small{4};
    const FontMetric* fonts[] = {&large, &medium, &small};

    const auto choice = sigurdos::ui::text_fit_choose_font(
        "123456", 42, ladder(fonts, 3), measure);

    EXPECT_EQ(choice.index, 1u);
    EXPECT_EQ(choice.font, &medium);
}

TEST(TextFitCore, FallsBackToSmallestForExtremelyLongText)
{
    const FontMetric large{10};
    const FontMetric medium{7};
    const FontMetric small{4};
    const FontMetric* fonts[] = {&large, &medium, &small};

    const auto choice = sigurdos::ui::text_fit_choose_font(
        "this string is still too long", 8, ladder(fonts, 3), measure);

    EXPECT_EQ(choice.index, 2u);
    EXPECT_EQ(choice.font, &small);
}

struct TranslationCase {
    StringId id;
    int max_width;
};

int codepoint_count(const char* text)
{
    int count = 0;
    for (const unsigned char* cursor =
             reinterpret_cast<const unsigned char*>(text ? text : "");
         *cursor;
         ++cursor) {
        if ((*cursor & 0xC0u) != 0x80u) ++count;
    }
    return count;
}

int translated_width(const char* text, const void* font, void*)
{
    const auto* metric = static_cast<const FontMetric*>(font);
    // The callback models the real LVGL measurement boundary while retaining
    // accented UTF-8 as one rendered glyph. The 8px rung represents the
    // compact fallback used for narrow translated labels.
    return codepoint_count(text) * metric->width;
}

TEST(TextFitCore, RepresentativeTranslationsFitTypicalBoxes)
{
    const FontMetric font_14{11};
    const FontMetric font_12{9};
    const FontMetric font_10{8};
    // 8px floor: measured from montserrat_8.c — A-Z mean adv_w 5.95px; the
    // longest tile strings (EINSTELLUNGEN, CONFIGURACIÓN, INSTELLINGEN)
    // measure ~70px in the real font (~5.4px/char). The int metric uses 5
    // px/char, a small safety margin without false-failing strings verified
    // on hardware.
    const FontMetric font_8{5};
    const FontMetric* fonts[] = {&font_14, &font_12, &font_10, &font_8};
    const TextFitFontLadder font_ladder = ladder(fonts, 4);

    const TranslationCase cases[] = {
        {StringId::HomeAdvertise, 76},
        {StringId::HomeSettings, 76},
        {StringId::HomeSetup, 76},
        {StringId::HomeSetupWarning, 76},
        {StringId::ChatRemovalNotSaved, 294},
        {StringId::ChatAddChannelRibbon, 120},
    };

    const Language languages[] = {
        Language::German, Language::French, Language::Spanish, Language::Dutch,
        Language::Italian, Language::Portuguese,
    };
    for (const auto language : languages) {
        for (const auto& test_case : cases) {
            const char* text = sigurdos::i18n::tr_for(language, test_case.id);
            const auto choice = sigurdos::ui::text_fit_choose_font(
                text, test_case.max_width, font_ladder, translated_width);
            ASSERT_LT(choice.index, font_ladder.count);
            EXPECT_LE(translated_width(text, choice.font, nullptr),
                      test_case.max_width)
                << "language=" << static_cast<int>(language)
                << " string=" << static_cast<int>(test_case.id);
        }
    }
}

} // namespace
