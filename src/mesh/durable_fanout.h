// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

namespace sigurdos::mesh {

using MessageFanoutFn = bool (*)(void* ctx);

struct MessageFanoutResult {
    bool durable;
    bool presented;
};

// Durable delivery is independent of bounded UI presentation. Both consumers
// are attempted, and the durable consumer is always invoked first.
inline MessageFanoutResult deliverMessageDurableFirst(
    MessageFanoutFn durable, MessageFanoutFn present, void* ctx)
{
    MessageFanoutResult result{false, false};
    if (durable) result.durable = durable(ctx);
    if (present) result.presented = present(ctx);
    return result;
}

} // namespace sigurdos::mesh
