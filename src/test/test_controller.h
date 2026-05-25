#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Remote test controller — allows controlling the T-Deck over serial for
// automated and manual testing. Enabled by SLOPOS_REMOTE_TEST=1 build flag.
//
// SAFETY: This module NEVER initializes the LoRa radio. No RF transmission
// occurs in remote test mode. All mesh messages are simulated via injection.

void slopos_test_controller_init();
void slopos_test_controller_loop();

// Handle a single command string (for programmatic use or parsing).
// Returns true if the command was recognised.
bool slopos_test_controller_exec(const char* cmd);

// ── New test controller API functions ──────────────────────
void slopos_test_controller_tap(int x, int y);
void slopos_test_controller_scroll(int x, int y, int dx, int dy);
