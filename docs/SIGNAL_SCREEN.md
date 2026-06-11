# Signal Screen

The Signal screen is a read-only dashboard displaying real-time radio statistics and current LoRa configuration. It provides a snapshot of link quality (RSSI, SNR, noise floor) alongside the operational parameters (frequency, bandwidth, spreading factor, coding rate, TX power).

---

## Source Files

| File | Purpose |
|------|---------|
| `src/ui/screens/screen_signal.cpp` | Implementation — `signal_screen_show()` |
| `src/ui/screens.h` | Public API — `signal_screen_show()` declaration |
| `src/mesh/mesh_wrapper.h` / `.cpp` | Runtime mesh queries — `getLastRSSI()`, `getLastSNR()`, `getNoiseFloor()` |
| `src/hal/prefs.h` / `.cpp` | Persisted `NodePrefs` (frequency, bandwidth, SF, CR, TX power) |

---

## Layout Structure

```
┌──────────────────────────────────┐
│ ←  #general  #random         14:32│  ← top bar (from make_screen_full)
├──────────────────────────────────┤
│                                  │
│         ┌──────────────┐         │
│         │  RSSI: -87 dBm│         │
│         │  SNR:   8.2 dB│         │
│         │  Noise: -112  │         │
│         │    dBm         │         │
│         │                │         │
│         │  Freq: 868.000│         │
│         │  BW:  125.0   │         │
│         │  SF:  12      │         │
│         │  CR:  4/5     │         │
│         │  TX Pwr: 20   │         │
│         └──────────────┘         │
│                                  │
├──────────────────────────────────┤
│ SigurdOS T-Deck   ▂▄▆█       72%  │  ← bottom bar (from make_screen_full)
└──────────────────────────────────┘
```

---

## Data Sources

| Metric | API Call | Type | Description |
|--------|----------|------|-------------|
| **RSSI** | `sigurdos::mesh::getLastRSSI()` | `int` (dBm) | Last received packet's signal strength. Negative values; closer to 0 = stronger. Typically −50 to −120 dBm. |
| **SNR** | `sigurdos::mesh::getLastSNR()` | `float` (dB) | Last received packet's signal-to-noise ratio. Positive = signal above noise floor. Typically −10 to +15 dB. |
| **Noise Floor** | `sigurdos::mesh::getNoiseFloor()` | `int` (dBm) | Background RF noise level measured by the SX1262. More negative = quieter band. |

### Radio Configuration (from `NodePrefs`)

| Parameter | Source Field | Example | Description |
|-----------|-------------|---------|-------------|
| **Frequency** | `p.freq` | `868.000 MHz` | Centre frequency in MHz (e.g. 868.0, 915.0) |
| **Bandwidth** | `p.bw` | `125.0 kHz` | LoRa channel bandwidth |
| **Spreading Factor** | `p.sf` | `12` | LoRa spreading factor (7–12) |
| **Coding Rate** | `p.cr` | `5` | Displayed as `4/5` (4 data bits + CR error-correction bits) |
| **TX Power** | `p.tx_power_dbm` | `20 dBm` | Transmit power in dBm |

### Unconfigured State

If the radio has not been configured (`p.configured == false`), the display reads:

```
RSSI:    -87 dBm
SNR:      8.2 dB
Noise:  -112 dBm

Radio:   NOT CONFIGURED
Go to Settings > Radio
to set frequency/power.
```

The user must navigate to **Settings > Radio** (`radio_setup_screen_show()`) to configure frequency, bandwidth, SF, CR, and TX power before the radio transmits.

---

## Implementation Details

### `signal_screen_show()` (`src/ui/screens/screen_signal.cpp`)

1. Calls `make_screen_full("Signal")` to construct the standard top bar + bottom bar chrome.
2. Queries three runtime metrics via the mesh wrapper:
   - `sigurdos::mesh::getLastRSSI()` — most recent RX RSSI
   - `sigurdos::mesh::getLastSNR()` — most recent RX SNR
   - `sigurdos::mesh::getNoiseFloor()` — current RF noise floor
3. Reads persisted `NodePrefs` via `sigurdos::prefs_get()`.
4. Composes a single multi-line label centred in the content area with `lv_font_montserrat_12`.
5. Conditional formatting:
   - **Configured** (`p.configured == true`): displays all 8 metrics (RSSI, SNR, noise, freq, BW, SF, CR, TX power).
   - **Unconfigured** (`p.configured == false`): only shows RSSI, SNR, noise floor plus a notice directing the user to the Radio setup screen.
6. Displays the screen via `show_screen(scr)` (slide-in animation).

### Update Behaviour

The Signal screen is a **static snapshot** — it reads current values at creation time and does **not** auto-refresh. To see updated values, navigate away and return.

---

## Related Screens

| Screen | Relationship |
|--------|-------------|
| **Radio Setup** (`radio_setup_screen_show`) | Write counterpart — configure the parameters displayed here (freq, BW, SF, CR, TX power) |
| **Network / Finder** ([NETWORK_SCREEN](NETWORK_SCREEN.md)) | Node discovery and ping — uses the same radio; RSSI shown per-node |
| **Packets** (`heard_screen_show`) | Per-packet RSSI and SNR in a scrollable log table |

---

## Key Constants & Ranges

| Metric | Typical Range | Interpretation |
|--------|---------------|----------------|
| RSSI | −50 to −120 dBm | >−70 excellent, >−85 good, >−100 fair, <−115 poor/noise |
| SNR | −10 to +15 dB | >+5 excellent, 0 to +5 marginal, <0 noisy link |
| Noise Floor | −90 to −130 dBm | More negative = quieter; industrial/urban areas are louder |

## Notes

- RSSI/SNR/noise floor values reflect the **most recently received packet** — not a running average.
- The `getNoiseFloor()` call returns the SX1262's RSSI value during a fixed-duration noise measurement (typically ~4ms). It does not require a received packet.
- All text is rendered in `TEXT_PRIMARY` (`#F2F3F5`, `montserrat_12`), centred in the content area with 8px horizontal padding.
