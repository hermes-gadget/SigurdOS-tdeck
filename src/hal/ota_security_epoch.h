// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos::hal {

static constexpr uint32_t SIGURDOS_OTA_APP_DESC_MAGIC = 0xABCD5432U;
static constexpr size_t SIGURDOS_OTA_APP_DESC_OFFSET = 32;
static constexpr size_t SIGURDOS_OTA_EPOCH_MIN_BYTES = 40;

enum class OtaEpochStatus : uint8_t { Allowed, Downgrade, Malformed };

inline uint32_t otaReadLe32(const uint8_t* bytes) {
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
           (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

inline OtaEpochStatus otaCheckSecurityEpoch(const uint8_t* image, size_t length,
                                            uint32_t current_epoch,
                                            uint32_t* incoming_epoch = nullptr) {
    if (incoming_epoch) *incoming_epoch = 0;
    if (!image || length < SIGURDOS_OTA_EPOCH_MIN_BYTES || image[0] != 0xE9 ||
        otaReadLe32(image + SIGURDOS_OTA_APP_DESC_OFFSET) !=
            SIGURDOS_OTA_APP_DESC_MAGIC) {
        return OtaEpochStatus::Malformed;
    }
    const uint32_t epoch = otaReadLe32(image + SIGURDOS_OTA_APP_DESC_OFFSET + 4);
    if (incoming_epoch) *incoming_epoch = epoch;
    return epoch < current_epoch ? OtaEpochStatus::Downgrade
                                 : OtaEpochStatus::Allowed;
}

}  // namespace sigurdos::hal
