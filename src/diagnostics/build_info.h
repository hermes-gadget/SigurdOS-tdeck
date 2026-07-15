// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Build identity exposed through telemetry, hardware validation, and release
// artifacts so field reports can be tied to exact firmware inputs.

#ifndef SIGURDOS_BUILD_INFO_H
#define SIGURDOS_BUILD_INFO_H

namespace sigurdos {
namespace build {

struct BuildInfo {
    const char* firmware_version;
    const char* git_sha;
    bool        git_dirty;
    const char* meshcore_sha;
    const char* build_env;
    const char* partitions;
    const char* board;
    const char* mcu;
    const char* build_source;
    const char* actions_run_id;
    const char* actions_run_attempt;
    const char* actions_ref;
    const char* actions_run_url;
};

const BuildInfo& info();

}  // namespace build
}  // namespace sigurdos

#endif  // SIGURDOS_BUILD_INFO_H
