// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone entry point for `pio run -e native_test`. PlatformIO's test
// runner supplies a strong GoogleTest main when executing `pio test`.

int __attribute__((weak)) main()
{
    return 0;
}
