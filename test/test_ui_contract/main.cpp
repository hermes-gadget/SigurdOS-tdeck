// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string read_project_file(const char* path)
{
    const char* prefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    for (const char* prefix : prefixes) {
        std::ifstream in(std::string(prefix) + path);
        if (in.good()) {
            return std::string(std::istreambuf_iterator<char>(in), {});
        }
    }
    return {};
}

TEST(UiContractTest, QrFailuresUseLoggingAndVisibleFeedback)
{
    const std::string channels = read_project_file("src/ui/screens/screen_channels.cpp");
    const std::string contacts = read_project_file("src/ui/screens/screen_contacts.cpp");
    ASSERT_FALSE(channels.empty());
    ASSERT_FALSE(contacts.empty());

    EXPECT_EQ(channels.find("Serial.print"), std::string::npos);
    EXPECT_EQ(contacts.find("Serial.print"), std::string::npos);
    EXPECT_NE(channels.find("SIG_LOGW(\"QR:"), std::string::npos);
    EXPECT_NE(contacts.find("SIG_LOGW(\"QR:"), std::string::npos);
    EXPECT_NE(channels.find("lv_label_set_text(label, \"ERR\")"), std::string::npos);
    EXPECT_NE(contacts.find("lv_label_set_text(label, \"QR unavailable\")"),
              std::string::npos);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
