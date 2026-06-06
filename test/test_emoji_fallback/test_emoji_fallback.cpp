// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>

#include "fonts/emoji_font.h"

namespace {

struct WrappedFont {
    const char* name;
    const lv_font_t* wrapped;
    const lv_font_t* base;
};

const WrappedFont* wrapped_fonts(size_t* count)
{
    static const WrappedFont fonts[] = {
        {"montserrat_10", emoji_wrapped_montserrat_10, &lv_font_montserrat_10},
        {"montserrat_12", emoji_wrapped_montserrat_12, &lv_font_montserrat_12},
        {"montserrat_14", emoji_wrapped_montserrat_14, &lv_font_montserrat_14},
        {"montserrat_16", emoji_wrapped_montserrat_16, &lv_font_montserrat_16},
        {"montserrat_18", emoji_wrapped_montserrat_18, &lv_font_montserrat_18},
        {"montserrat_20", emoji_wrapped_montserrat_20, &lv_font_montserrat_20},
        {"montserrat_24", emoji_wrapped_montserrat_24, &lv_font_montserrat_24},
        {"montserrat_28", emoji_wrapped_montserrat_28, &lv_font_montserrat_28},
    };
    *count = sizeof(fonts) / sizeof(fonts[0]);
    return fonts;
}

TEST(EmojiFallback, WrappedFontPointersArePresent) {
    size_t count = 0;
    const WrappedFont* fonts = wrapped_fonts(&count);
    ASSERT_EQ(count, 8u);

    for (size_t i = 0; i < count; i++) {
        EXPECT_NE(fonts[i].wrapped, nullptr) << fonts[i].name;
    }
}

TEST(EmojiFallback, WrappedFontsAreWritableCopies) {
    size_t count = 0;
    const WrappedFont* fonts = wrapped_fonts(&count);

    for (size_t i = 0; i < count; i++) {
        ASSERT_NE(fonts[i].wrapped, nullptr) << fonts[i].name;
        EXPECT_NE(fonts[i].wrapped, fonts[i].base) << fonts[i].name;
    }
}

TEST(EmojiFallback, RegistrationSetsFallbackChainForAllWrappers) {
    emoji_font_register_fallback();

    size_t count = 0;
    const WrappedFont* fonts = wrapped_fonts(&count);

    for (size_t i = 0; i < count; i++) {
        ASSERT_NE(fonts[i].wrapped, nullptr) << fonts[i].name;
        // Each Montserrat wrapper's fallback now points to the latin_ext
        // wrapper (not directly to emoji_font). The chain is:
        //   wrapped_N → wrapped_latin_ext → emoji_font
        EXPECT_NE(fonts[i].wrapped->fallback, nullptr) << fonts[i].name;
        EXPECT_NE(fonts[i].wrapped->fallback, fonts[i].wrapped) << fonts[i].name;
    }
}

} // anonymous namespace
