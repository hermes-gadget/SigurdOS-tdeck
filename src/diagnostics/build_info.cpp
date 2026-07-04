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

#ifndef SIGURDOS_BUILD_SOURCE
#define SIGURDOS_BUILD_SOURCE "local"
#endif

#ifndef SIGURDOS_BUILD_ACTIONS_RUN_ID
#define SIGURDOS_BUILD_ACTIONS_RUN_ID "local"
#endif

#ifndef SIGURDOS_BUILD_ACTIONS_RUN_ATTEMPT
#define SIGURDOS_BUILD_ACTIONS_RUN_ATTEMPT ""
#endif

#ifndef SIGURDOS_BUILD_ACTIONS_REF
#define SIGURDOS_BUILD_ACTIONS_REF "unknown"
#endif

#ifndef SIGURDOS_BUILD_ACTIONS_RUN_URL
#define SIGURDOS_BUILD_ACTIONS_RUN_URL ""
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
        SIGURDOS_BUILD_SOURCE,
        SIGURDOS_BUILD_ACTIONS_RUN_ID,
        SIGURDOS_BUILD_ACTIONS_RUN_ATTEMPT,
        SIGURDOS_BUILD_ACTIONS_REF,
        SIGURDOS_BUILD_ACTIONS_RUN_URL,
    };
    return kInfo;
}

}  // namespace build
}  // namespace sigurdos
