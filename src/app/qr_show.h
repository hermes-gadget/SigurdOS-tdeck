#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// QR code display screen for SigurdOS.
// Uses ricmoo's QRCode library (MIT) to render QR codes on a full-screen LVGL canvas.

namespace sigurdos {
namespace app {

/// Show a full-screen QR code displaying the given data string.
/// The screen has a themed top bar with a back button and a bottom bar.
/// The QR code is auto-sized and centered in the content area.
/// @param title  Text shown in the top bar (e.g. "Contact Key" or "Channel Secret")
/// @param data   Data string to encode as a QR code (text, URI, hex key, etc.)
void qr_show(const char* title, const char* data);

} // namespace app
} // namespace sigurdos
