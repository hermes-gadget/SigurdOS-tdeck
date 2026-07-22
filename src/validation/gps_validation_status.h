#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sigurdos::gps_validation {

struct StatusFields {
  uint32_t millis;
  bool has_fix;
  uint8_t fix_quality;
  uint8_t satellites;
  uint8_t satellites_in_view;
  uint8_t fix_type;
  char rmc_status;
  uint8_t snr_max;
  uint8_t snr_count;
  uint32_t baud;
  uint32_t chars;
  uint32_t sentences;
  uint32_t valid;
  uint32_t gga;
  uint32_t rmc;
  uint32_t gsv;
  uint32_t gsa;
  uint32_t checksum_failures;
  uint32_t baud_switches;
  float latitude;
  float longitude;
};

inline bool buildStatus(char* out, size_t out_size, const StatusFields& f,
                        bool include_coordinates) {
  if (!out || out_size == 0) return false;
  const int written = snprintf(
      out, out_size,
      "@gps_hw|ms=%lu|fix=%u|qual=%u|sv=%u|siv=%u|ft=%u|rmc=%c|snr=%u|snrc=%u|baud=%lu|chars=%lu|sent=%lu|valid=%lu|gga=%lu|rmc_s=%lu|gsv=%lu|gsa=%lu|csfail=%lu|sw=%lu|loc=%u",
      static_cast<unsigned long>(f.millis), f.has_fix ? 1U : 0U,
      static_cast<unsigned>(f.fix_quality), static_cast<unsigned>(f.satellites),
      static_cast<unsigned>(f.satellites_in_view), static_cast<unsigned>(f.fix_type),
      f.rmc_status ? f.rmc_status : '-', static_cast<unsigned>(f.snr_max),
      static_cast<unsigned>(f.snr_count), static_cast<unsigned long>(f.baud),
      static_cast<unsigned long>(f.chars), static_cast<unsigned long>(f.sentences),
      static_cast<unsigned long>(f.valid), static_cast<unsigned long>(f.gga),
      static_cast<unsigned long>(f.rmc), static_cast<unsigned long>(f.gsv),
      static_cast<unsigned long>(f.gsa),
      static_cast<unsigned long>(f.checksum_failures),
      static_cast<unsigned long>(f.baud_switches), f.has_fix ? 1U : 0U);
  if (written < 0 || static_cast<size_t>(written) >= out_size) {
    out[out_size - 1] = '\0';
    return false;
  }

  if (include_coordinates && f.has_fix) {
    const size_t used = static_cast<size_t>(written);
    const int appended = snprintf(out + used, out_size - used, "|lat=%.6f|lon=%.6f",
                                  static_cast<double>(f.latitude),
                                  static_cast<double>(f.longitude));
    if (appended < 0 || static_cast<size_t>(appended) >= out_size - used) {
      out[out_size - 1] = '\0';
      return false;
    }
  }
  return true;
}

}  // namespace sigurdos::gps_validation
