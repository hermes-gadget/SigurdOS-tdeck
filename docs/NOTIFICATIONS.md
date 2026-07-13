# Notifications and Alerts

SigurdOS has a bounded four-entry notification queue for field events that
should remain visible outside their source screen. The renderer uses the
runtime theme and recreates the active banner safely after navigation.

## Event behaviour

| Event | UI | Sound |
|---|---|---|
| Direct message | 4-second cyan toast | Short beep |
| Channel mention (whole node-name match, case-insensitive) | 6-second orange toast and orange unread badge | Double beep |
| Other channel message | Unread badge | Double beep |
| Companion connect/disconnect | 4-second toast and live Bluetooth top-bar icon | None |
| Repeater/room login result | 4-second success or 6-second failure toast | None |
| Battery at or below 15% | Sticky red banner | None |
| SD free space at or below 1 MiB or 2% | Sticky red banner | None |
| GitHub OTA failure | Sticky red banner containing the bounded error string | None |

Timed alerts expire automatically. Sticky alerts and any current timed alert
can be dismissed by tapping the banner. The four-entry limit prevents bursts
from growing UI memory without bound; a higher-priority event temporarily
preempts the current alert.

The existing Settings → Radio / Mesh quiet-buzzer preference suppresses all
message notification sounds. Notifications do not wake a sleeping display and
are not persisted across reboot.

## Sources and tests

- `src/ui/notification_model.h` — priority, expiry, mention, and threshold rules
- `src/ui/notifications.cpp` — state observers, message/login hooks, LVGL banner
- `src/ui/screens_common.cpp` — live companion Bluetooth top-bar icon
- `test/test_notifications/main.cpp` — native policy and queue coverage
