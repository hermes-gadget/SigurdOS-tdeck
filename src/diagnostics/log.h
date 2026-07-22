#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Lightweight serial logging macros. Debug logs compile out of release builds.

#include <Arduino.h>
#include "debug_cfg.h"
#include "diagnostic_io.h"

#if defined(SIGURDOS_COMPANION_USB) && SIGURDOS_COMPANION_USB
#define SIG_LOGE(fmt, ...) do { } while (0)
#define SIG_LOGW(fmt, ...) do { } while (0)
#define SIG_LOGD(fmt, ...) do { } while (0)
#else
#define SIG_LOGE(fmt, ...) sigurdos::diagnostics::diagnostic_logf("[E] ", fmt, ##__VA_ARGS__)
#define SIG_LOGW(fmt, ...) sigurdos::diagnostics::diagnostic_logf("[W] ", fmt, ##__VA_ARGS__)

#if SIGURDOS_DEBUG_ACTIVE
#define SIG_LOGD(fmt, ...) sigurdos::diagnostics::diagnostic_logf("[D] ", fmt, ##__VA_ARGS__)
#else
#define SIG_LOGD(fmt, ...) do { } while (0)
#endif
#endif
