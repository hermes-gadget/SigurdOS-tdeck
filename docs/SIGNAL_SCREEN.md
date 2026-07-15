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
│  ┌─────────────┐  ┌────────────┐ │
│  │ TX Flood:    │  │ RX Flood:  │ │
│  │       1,234  │  │      5,678 │ │
│  │ TX Direct:   │  │ RX Direct: │ │
│  │         567  │  │        890 │ │
│  │ Airtime:     │  │ Duty Cycle:│ │
│  │     12.3%    │  │      4.5%  │ │
│  └─────────────┘  └────────────┘ │
│                                  │
│  ┌──────────────────────────┐    │
│  │      RSSI History        │    │
│  │  ▁▂▃▅▇▆▄▃▂▁▃▄▅▆▇▆▅▄▃▂   │    │
│  │  -70   -85   -95  -110   │    │
│  └──────────────────────────┘    │
│                                  │
│  Freq: 868.000  SF: 12  BW: 125  │
│  CR: 4/5  TX Pwr: 20 dBm        │
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
2. Retrieves per-category packet counts:
   - `sigurdos::mesh::getNumSentFlood()` / `getNumRecvFlood()` — flood message counts
   - `sigurdos::mesh::getNumSentDirect()` / `getNumRecvDirect()` — direct message counts
   - `sigurdos::mesh::getTotalTxAirtimeMs()` / `getTotalRxAirtimeMs()` — airtime totals
3. Computes duty cycle and historical RSSI data for the sparkline chart.
4. Displays results in a **two-column statistics layout** with an optional RSSI history sparkline.
5. Reads persisted `NodePrefs` via `sigurdos::prefs_get()` for the radio parameters row (freq, SF, BW, CR, TX power).
6. Conditional formatting:
   - **Configured** (`p.configured == true`): displays all stats, chart, and radio parameters.
   - **Unconfigured** (`p.configured == false`): shows packet counts and a notice directing the user to the Radio setup screen.
7. Displays the screen via `show_screen(scr)` (slide-in animation).

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
| TX/RX Counts | 0–100,000+ | Per-category packet counts (flood, direct) |
| Airtime | 0–100% | Percentage of time radio spent transmitting/receiving |
| Duty Cycle | 0–100% | Regulatory TX duty-cycle limit enforcement |
| RSSI History | −50 to −120 dBm | Historical sparkline of last N RSSI samples |

## Notes

- The display is a **static snapshot** with live counters — values update as the mesh processes packets but the screen does not auto-refresh.
- RSSI history is stored as a ring buffer of recent packet RSSI values for the sparkline chart.
- Radio parameters are read from persisted `NodePrefs` at screen creation time.
