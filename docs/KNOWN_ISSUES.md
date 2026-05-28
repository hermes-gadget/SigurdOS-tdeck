# Known Issues

This document tracks currently open known issues, bugs, and missing features in SlopOS T-Deck firmware. All historically tracked issues that have been resolved are maintained in the Git history — check merged PRs and commit logs for the full record.

---

## Launcher Compatibility

### SlopOS doesn't work under bmorcelli/Launcher

[Launcher](https://github.com/bmorcelli/Launcher) is an ESP32 app launcher with explicit T-Deck support (display, touch, keyboard, SD card). A user tried running SlopOS as a Launcher-launched app and ran into problems — the keyboard doesn't work properly, and many other things break.

**Root cause:** SlopOS is built as standalone firmware that expects full hardware control at boot. Launcher initialises the display, keyboard, I2C, SPI, and LoRa pins before handing off, which leaves GPIOs, peripheral registers, and I2C bus state in an incompatible state when SlopOS starts.

**Status:** Not planned. SlopOS is designed as standalone firmware, not a Launcher app. Fixing this would require deep changes to every HAL driver to detect and handle pre-initialised peripherals.

---

## How to Help

Pick any item from the list above and open a PR against the `dev` branch. See [`CONTRIBUTING.md`](./CONTRIBUTING.md) for the full contribution workflow.
