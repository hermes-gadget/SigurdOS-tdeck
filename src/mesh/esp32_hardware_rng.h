// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <Utils.h>
#include <esp_random.h>

#include <cstddef>
#include <cstdint>

namespace sigurdos::mesh {

// Cryptographic random source for identity keys and MeshCore protocol values.
// Unlike MeshCore's StdRNG, this never seeds or reads Arduino's libc-backed
// random() stream.
class Esp32HardwareRng final : public ::mesh::RNG {
 public:
  void random(uint8_t* dest, size_t size) override {
    if (dest == nullptr || size == 0) {
      return;
    }
    esp_fill_random(dest, size);
  }
};

}  // namespace sigurdos::mesh
