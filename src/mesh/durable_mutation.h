// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

namespace sigurdos {
namespace mesh {
namespace detail {

// Apply a runtime mutation, commit its durable representation, and restore the
// old runtime state if the commit fails. Callers provide small injected steps
// so write-failure behaviour can be exercised in native tests.
template <typename Apply, typename Commit, typename Rollback>
bool applyAndCommit(Apply apply, Commit commit, Rollback rollback)
{
    if (!apply()) return false;
    if (commit()) return true;
    rollback();
    return false;
}

// Identity changes are safer in the opposite order: persist a fully validated
// candidate first, then activate it and invalidate identity-derived state.
template <typename Commit, typename Activate>
bool commitBeforeActivate(Commit commit, Activate activate)
{
    if (!commit()) return false;
    activate();
    return true;
}

} // namespace detail
} // namespace mesh
} // namespace sigurdos
