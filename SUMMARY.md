# Data-integrity fixes

Branch: `fix/audit-data-integrity`

## Fixes

- #1467: Companion preference setters now report persistence failures. Config commands return a file-I/O error, and BLE enablement rolls back its live state when persistence fails.
- #1471: GPS GGA time-of-day updates no longer pair a new UTC day with the previous RMC date.
- #1472: Keyboard key-mode decoding updates letter-case state for every decoded byte, including plain and shifted digits.
- #1473: Unread tracking uses the canonical 32-conversation mesh/DM capacity instead of stopping at 16 entries.
- #1477: UTF-8 truncation bounds both the NUL scan and malformed-sequence validation to the actual string length.

## Files changed

- #1467: `src/comms/companion_bridge.h`, `src/comms/companion_bridge.cpp`, `src/mesh/companion_adapter.cpp`, `test/test_companion_protocol/test_companion_protocol.cpp`
- #1471: `src/hal/gps.cpp`, `test/test_gps/test_gps.cpp`
- #1472: `src/hal/keyboard.cpp`, `test/test_keyboard/test_keyboard.cpp`
- #1473: `src/ui/chat_unread_store.h`, `test/test_chat_config/test_chat_config.cpp`
- #1477: `src/utils/utf8_util.h`, `test/test_chat_truncation/main.cpp`

## Tests added

- #1467: `OtherParamsPersistenceFailureIsReported`, `AutoAddPersistenceFailureIsReported`
- #1471: `GgaCannotPairNewUtcDayWithPreviousRmcDate`
- #1472: `KeyModePlainDigitClearsPreviousLetterShiftState`, `KeyModeShiftedDigitPublishesShiftState`
- #1473: `TracksAllCanonicalMeshAndDmConversations`
- #1477: `NullTerminatedShortMalformedSequencesStayWithinAllocation`

## Verification

- `pio test -e native_test -v` — passed: 1,568 succeeded, 1 skipped
- `pio test -e native_sanitize -f test_chat_truncation -v` — passed: 24 succeeded
- `pio run -e SigurdOS_TDeck` — passed
