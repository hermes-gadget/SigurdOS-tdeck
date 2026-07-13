// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#pragma once

namespace sigurdos::ui {

class MapTrackballPanState {
public:
    void reset() { panned_ = false; }
    void notePan() { panned_ = true; }

    bool consumeLeftPan()
    {
        if (!panned_) return false;
        panned_ = false;
        return true;
    }

private:
    bool panned_ = false;
};

} // namespace sigurdos::ui
