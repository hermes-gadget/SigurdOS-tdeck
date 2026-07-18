// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// `pio run -e native_test` builds the native source filter without selecting a
// GoogleTest module. Give that build a link entry point while leaving each
// `pio test` module's own main() in control during unit-test builds.
#if !defined(PIO_UNIT_TESTING)
int main()
{
    return 0;
}
#endif
