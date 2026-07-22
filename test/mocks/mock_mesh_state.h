#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

namespace sigurdos::mesh {

// Restores every process-global value owned by mock_mesh_wrapper.cpp.
// Fixtures should call this before exercising mock-backed behavior.
void mock_reset_all();
void mock_set_noise(int value);
void mock_set_rssi(int value);
void mock_set_snr(float value);
void mock_set_drop_count(uint32_t value);

}  // namespace sigurdos::mesh
