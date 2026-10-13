// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Native-test compatibility shim. The ESP32 Arduino core provides ltoa();
// the shared native mocks do not, so this header (force-included via
// `-include` in the native_contact_index env) supplies an inline version.
// Written to compile as both C and C++.

#ifndef SIGURDOS_NATIVE_COMPAT_H
#define SIGURDOS_NATIVE_COMPAT_H

#ifndef SIGURDOS_HAVE_LTOA
#define SIGURDOS_HAVE_LTOA

static inline char* ltoa(long value, char* buffer, int base) {
    char tmp[34];
    char* out = tmp + sizeof(tmp);
    unsigned long v;
    int negative = 0;

    if (base == 10 && value < 0) {
        negative = 1;
        v = (unsigned long)(-(value + 1)) + 1UL;
    } else {
        v = (unsigned long)value;
    }

    *--out = '\0';
    do {
        unsigned int digit = (unsigned int)(v % (unsigned int)base);
        *--out = (char)(digit < 10 ? ('0' + digit) : ('a' + digit - 10));
        v /= (unsigned long)base;
    } while (v != 0);
    if (negative) {
        *--out = '-';
    }

    {
        char* p = buffer;
        while ((*p++ = *out++) != '\0') {
        }
    }
    return buffer;
}

#endif /* SIGURDOS_HAVE_LTOA */

#endif /* SIGURDOS_NATIVE_COMPAT_H */
