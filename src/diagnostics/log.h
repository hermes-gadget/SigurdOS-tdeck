#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Lightweight serial logging macros. Debug logs compile out of release builds.

#include <Arduino.h>

#define SIG_LOGE(fmt, ...) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__)
#define SIG_LOGW(fmt, ...) Serial.printf("[W] " fmt "\n", ##__VA_ARGS__)

#if defined(SIGURDOS_DEBUG)
#define SIG_LOGD(fmt, ...) Serial.printf("[D] " fmt "\n", ##__VA_ARGS__)
#else
#define SIG_LOGD(fmt, ...) do { } while (0)
#endif
