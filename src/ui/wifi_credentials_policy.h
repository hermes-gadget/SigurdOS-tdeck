#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstring>

namespace sigurdos::ui {

struct WifiCredentialStage {
    char ssid[33]{};
    char password[64]{};
    bool valid = false;
};

inline bool wifi_credentials_stage(WifiCredentialStage& stage,
                                   const char* ssid, const char* password)
{
    if (!ssid || !ssid[0] || !password) return false;
    std::strncpy(stage.ssid, ssid, sizeof(stage.ssid) - 1);
    stage.ssid[sizeof(stage.ssid) - 1] = '\0';
    std::strncpy(stage.password, password, sizeof(stage.password) - 1);
    stage.password[sizeof(stage.password) - 1] = '\0';
    stage.valid = true;
    return true;
}

inline bool wifi_credentials_commit(const WifiCredentialStage& stage, bool connected,
                                    char* ssid_out, size_t ssid_size,
                                    char* password_out, size_t password_size)
{
    if (!connected || !stage.valid || !ssid_out || ssid_size == 0 ||
        !password_out || password_size == 0) return false;
    std::strncpy(ssid_out, stage.ssid, ssid_size - 1);
    ssid_out[ssid_size - 1] = '\0';
    std::strncpy(password_out, stage.password, password_size - 1);
    password_out[password_size - 1] = '\0';
    return true;
}

} // namespace sigurdos::ui
