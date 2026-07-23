// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "ui/file_browser_model.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>

namespace {

std::string readProjectFile(const char* path)
{
    const char* prefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    for (const char* prefix : prefixes) {
        std::ifstream input(std::string(prefix) + path);
        if (input.good()) {
            return std::string(std::istreambuf_iterator<char>(input), {});
        }
    }
    return {};
}

TEST(FileBrowserModelTest, JoinsRootAndNestedPaths)
{
    char path[SIGURDOS_SD_MAX_PATH_LEN + 1];
    EXPECT_TRUE(sigurdos::ui::file_browser::joinPath("/", "maps", path, sizeof(path)));
    EXPECT_STREQ(path, "/maps");
    EXPECT_TRUE(sigurdos::ui::file_browser::joinPath(
        "/maps", "tile.png", path, sizeof(path)));
    EXPECT_STREQ(path, "/maps/tile.png");
}

TEST(FileBrowserModelTest, RejectsTraversalAndOverflow)
{
    char path[12];
    EXPECT_FALSE(sigurdos::ui::file_browser::joinPath("/", "../secret", path, sizeof(path)));
    EXPECT_FALSE(sigurdos::ui::file_browser::joinPath("/", "folder/file", path, sizeof(path)));
    EXPECT_FALSE(sigurdos::ui::file_browser::joinPath(
        "/folder", "very-long-name.txt", path, sizeof(path)));
}

TEST(FileBrowserModelTest, ParentStopsAtRoot)
{
    char path[SIGURDOS_SD_MAX_PATH_LEN + 1];
    EXPECT_TRUE(sigurdos::ui::file_browser::parentPath(
        "/maps/12", path, sizeof(path)));
    EXPECT_STREQ(path, "/maps");
    EXPECT_TRUE(sigurdos::ui::file_browser::parentPath("/maps", path, sizeof(path)));
    EXPECT_STREQ(path, "/");
    EXPECT_TRUE(sigurdos::ui::file_browser::parentPath("/", path, sizeof(path)));
    EXPECT_STREQ(path, "/");
}

TEST(FileBrowserModelTest, CopyNamePreservesExtension)
{
    char name[SIGURDOS_SD_MAX_NAME_LEN + 1];
    EXPECT_TRUE(sigurdos::ui::file_browser::copyName(
        "track.gpx", 1, name, sizeof(name)));
    EXPECT_STREQ(name, "track copy.gpx");
    EXPECT_TRUE(sigurdos::ui::file_browser::copyName(
        "track.gpx", 3, name, sizeof(name)));
    EXPECT_STREQ(name, "track copy 3.gpx");
    EXPECT_TRUE(sigurdos::ui::file_browser::copyName(
        "README", 1, name, sizeof(name)));
    EXPECT_STREQ(name, "README copy");
}

TEST(FileBrowserModelTest, SdOperationApisHaveStableSignatures)
{
    using list_fn = bool (*)(const char*, SigurdosSdDirEntry*, size_t, size_t*, bool*);
    using path_fn = bool (*)(const char*);
    using copy_fn = bool (*)(const char*, const char*);

    static_assert(std::is_same_v<decltype(&sigurdos_sdcard_list), list_fn>);
    static_assert(std::is_same_v<decltype(&sigurdos_sdcard_delete_file), path_fn>);
    static_assert(std::is_same_v<decltype(&sigurdos_sdcard_copy_file), copy_fn>);
    SUCCEED();
}

TEST(FileBrowserScreenContractTest, ExposesMetadataSelectionAndFileActions)
{
    const std::string source = readProjectFile(
        "src/ui/screens/screen_file_browser.cpp");
    ASSERT_FALSE(source.empty());

    EXPECT_NE(source.find("entry.size_bytes"), std::string::npos);
    EXPECT_NE(source.find("entry.modified_time"), std::string::npos);
    EXPECT_NE(source.find("lv_obj_set_style_border_color(row"), std::string::npos);
    EXPECT_NE(source.find("LV_SYMBOL_EYE_OPEN \" Open\""), std::string::npos);
    EXPECT_NE(source.find("LV_SYMBOL_COPY \" Copy\""), std::string::npos);
    EXPECT_NE(source.find("LV_SYMBOL_TRASH \" Delete\""), std::string::npos);
    EXPECT_NE(source.find("file_browser::parentPath"), std::string::npos);
}

} // namespace
