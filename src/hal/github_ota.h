// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// GitHub OTA firmware update — connects to WiFi in STA mode, fetches the
// latest release (matching the user's branch and pre-release preference)
// from GitHub, and flashes via the Arduino Update class.
//
// The release is selected by:
//   - Branch: "dev" or "main" (compares against target_commitish)
//   - Pre-release: if enabled, includes pre-release tags in the search

#pragma once
#include <cstdint>

namespace sigurdos {
namespace github_ota {

// ── GitHub OTA state ────────────────────────────────────────────

enum class GitHubOTAState {
    Idle,
    Connecting,     // Connecting to WiFi
    FetchingRelease,// Fetching release info from GitHub API
    Downloading,    // Downloading firmware from GitHub
    Writing,        // Writing to flash (Update class)
    Success,        // Done — rebooting
    Failed,         // Error occurred
};

struct GitHubOTAStatus {
    GitHubOTAState state = GitHubOTAState::Idle;
    int  progress_pct = 0;       // 0-100 (download + write combined)
    char status_msg[80] = "";   // Human-readable status
    char error_msg[128] = "";   // Last error (when state == Failed)
};

// ── Public API ──────────────────────────────────────────────────

// Start GitHub OTA update asynchronously.
// Connects to WiFi using saved NVS credentials, fetches release info,
// downloads firmware.bin from the matching release, and flashes it.
// Returns true if started (or already running).
bool startGitHubUpdate();

// Call in main loop to service WiFi, HTTP, and release-fetch state.
void loop();

// Returns true if OTA is currently active.
bool isActive();

// Returns the current status (call from UI to show progress).
const GitHubOTAStatus& getStatus();

// Cancel an in-progress OTA (best effort).
void cancel();

// ── Settings helpers (usable from UI without starting an update) ──

// Return the current firmware download URL (for display).
// Depends on saved prefs (ota_branch, ota_allow_prerelease).
const char* getDownloadLabel();

// The URL label buffer is small — for display only.
// The actual download uses the full API to find the release.

}  // namespace github_ota
}  // namespace sigurdos
