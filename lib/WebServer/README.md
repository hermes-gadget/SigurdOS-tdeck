# Reviewed WebServer security overlay

This local library is copied from `framework-arduinoespressif32`
`3.20017.241212+sha.dcc1105b` (Arduino-ESP32 2.0.17) and takes precedence over
the framework-bundled WebServer during PlatformIO dependency resolution.

It carries the multipart-boundary limit and complete response-header CR/LF
sanitization required by `scripts/check_security_patches.py`. The verifier
pins the framework version and the full hashes of both patched implementation
files. Update this overlay and those hashes together during a reviewed
framework upgrade; never patch the shared PlatformIO package cache.

The upstream source headers retain the GNU LGPL 2.1-or-later license notice.
