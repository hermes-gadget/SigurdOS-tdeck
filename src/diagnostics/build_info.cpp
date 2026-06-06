// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "build_info.h"
#include "../hal/tdeck_pins.h"

#ifndef SIGURDOS_BUILD_GIT_SHA
#define SIGURDOS_BUILD_GIT_SHA "unknown"
#endif

#ifndef SIGURDOS_BUILD_GIT_DIRTY
#define SIGURDOS_BUILD_GIT_DIRTY 0
#endif

#ifndef SIGURDOS_BUILD_MESHCORE_SHA
#define SIGURDOS_BUILD_MESHCORE_SHA "unknown"
#endif

#ifndef SIGURDOS_BUILD_ENV
#define SIGURDOS_BUILD_ENV "unknown"
#endif

#ifndef SIGURDOS_BUILD_PARTITIONS
#define SIGURDOS_BUILD_PARTITIONS "unknown"
#endif

#ifndef SIGURDOS_BUILD_BOARD
#define SIGURDOS_BUILD_BOARD "unknown"
#endif

#ifndef SIGURDOS_BUILD_MCU
#define SIGURDOS_BUILD_MCU "unknown"
#endif

namespace sigurdos {
namespace build {

const BuildInfo& info()
{
    static const BuildInfo kInfo = {
        SIGURDOS_VERSION,
        SIGURDOS_BUILD_GIT_SHA,
        SIGURDOS_BUILD_GIT_DIRTY != 0,
        SIGURDOS_BUILD_MESHCORE_SHA,
        SIGURDOS_BUILD_ENV,
        SIGURDOS_BUILD_PARTITIONS,
        SIGURDOS_BUILD_BOARD,
        SIGURDOS_BUILD_MCU,
    };
    return kInfo;
}

}  // namespace build
}  // namespace sigurdos
