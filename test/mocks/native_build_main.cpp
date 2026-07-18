// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// `pio run -e native_test` builds the shared native sources without selecting
// a test suite, so PlatformIO does not add any suite's main.cpp. Keep a tiny
// entry point for that compile-only check and exclude it from actual tests.
#if !defined(PIO_UNIT_TESTING)
int main()
{
    return 0;
}
#endif
