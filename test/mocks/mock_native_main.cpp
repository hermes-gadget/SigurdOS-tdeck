// SPDX-License-Identifier: GPL-3.0-or-later

// PlatformIO's native `run` build links the production/native mocks without a
// test module, so provide an entry point for that compile check. Unit test
// builds define PIO_UNIT_TESTING and use each suite's own main().
#ifndef PIO_UNIT_TESTING
int main() {
    return 0;
}
#endif
