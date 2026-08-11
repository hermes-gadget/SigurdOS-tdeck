// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

// The native source filter intentionally excludes the SD adapter. Compile it
// into this focused module so the coordinator can exercise the real runtime
// SD-to-SPIFFS demotion path.
#include "../../src/mesh/sd_message_store.cpp"

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
