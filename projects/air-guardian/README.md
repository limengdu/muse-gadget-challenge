<!--
Copyright (c) 2026 limengdu

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# Outdoor Air — SenseCAP Indicator D1

[简体中文](README.zh-CN.md) · [Muse skill](SKILL.md) · [Validation](VALIDATION.md)

A calm outdoor air advisor for the family at home. Muse looks up outdoor air
for the owner's chosen place using its own tools and preferences, then pushes
one snapshot to the D1's 3.95-inch, 480×480 LCD. The board retains the last good
snapshot and selected view across restarts.

## Hardware and data

Supported hardware is **SenseCAP Indicator D1 only**, using the Link-only
`tools/board.sh sensecap-indicator` profile. D1S and D1 Pro are outside this
application's support scope. D1 has no onboard CO2/tVOC sensors. This project
uses no indoor sensors, Grove AHT20, RP2040 readings or sensor UART data.

The shared board overlay keeps its existing `CONFIG_HOMEHUB_SENSECAP_SENSORS`
setting for the base SDK. This app skips the UART reader and operates independently with every reading
absent or null. While this app is enabled, registration exposes outdoor commands
and omits the base indoor sensor command. There is no indoor history ring,
on-device weather lookup, automatic refresh scheduler, or category notification.
Muse owns lookups, routines and concise advice; the device quietly displays them.

## Build

From `esp32/` with ESP-IDF **v6.0.1**:

```sh
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf6.0_py3.14_env
tools/board.sh sensecap-indicator build
```

The overlay enables the opt-in `CONFIG_HOMEHUB_AIR_GUARDIAN` app flag, the local
clock and shared card assets. Its Kconfig dependency is the Indicator LCD,
PSRAM and daemon requests. Internal `air_guardian_*` filenames and this folder
retain their existing paths. Configure device credentials privately in the
ignored build directory using `idf.py -B build-sensecap-indicator menuconfig`.
Hardware deployment follows the SDK's [device guide](../../esp32/AGENTS.md).

## Muse setup

Give [SKILL.md](SKILL.md) to Muse as the **Outdoor Air** skill. Tell Muse the
place and household preferences, then ask it to look up current outdoor air
and update this gadget. A morning refresh or another schedule runs in Muse.

Muse should establish actual current UTC and the owner's current local offset
with `clock.set {"utc_epoch":...,"utc_offset_min":...}`. Observation time comes
from the source; it remains unchanged when the same report is pushed again.

## Commands

| Command | Effect |
|---|---|
| `air.update` | Validate and save a complete outdoor snapshot. |
| `air.status {}` | Return the saved snapshot, `age_s`, `refresh_needed`, clock validity and selected view. |
| `air.set_view {"view":"numbers"}` | Save `ambient`, `numbers` or `night`. |

Illustrative `air.update` payload; replace values and timestamp with the actual
source observation:

```json
{
  "aqi": 42,
  "pm25": 7.1,
  "category": "good",
  "advice": "Enjoy your usual plans.",
  "place": "San Francisco",
  "updated_utc": 1793793600
}
```

- Supply `aqi`, `pm25`, or both. Omitted or null metrics are absent. AQI is an
  integer **0–500 on the US EPA scale**; PM2.5 is **0–1000 ug/m3**. Zero is valid.
- `category`, `advice`, and `updated_utc` are required. Category is one of
  `good`, `moderate`, `unhealthy_sensitive`, `unhealthy`, `very_unhealthy`,
  `hazardous`. With AQI, it must match bands 0–50, 51–100, 101–150, 151–200,
  201–300, 301–500 respectively ([AirNow categories](https://www.airnow.gov/aqi/aqi-basics/)).
  With only PM2.5, Muse supplies the source's corresponding US EPA category,
  accounting for its observation period.
- `advice` accepts 1–80 printable ASCII characters and `place` optionally accepts
  1–40. Short display text works best, around 25 characters for advice. Longer
  text is fitted with an ellipsis. Use an English or transliterated city label.
- `updated_utc` is integer observation UTC Unix seconds, 1704067200–4102444799.
  An older observation is rejected; a timestamp more than five minutes ahead
  of a valid device clock is rejected. Unknown fields and duplicate keys fail.
- Each update fully replaces the snapshot, including optional fields. Successful
  writes save the snapshot and view together in one NVS record (`outdoor_air`).
  Validation or storage errors leave the previous saved result in use.

Status returns `snapshot: null` before the first update. `age_s` is elapsed
seconds since observation, or null when the clock is unknown or behind the
observation. `refresh_needed` is true with no known age or after three hours.
A restored snapshot retains its actual observation time.

## Screen and button

Ambient shows category, large AQI (or PM2.5 when AQI is absent), advice, place,
`Updated hh:mm` and a category colour band. Numbers shows both metrics, with
`--` for the missing one. Night uses dim warm pixels and a dim category band;
physical backlight brightness remains the board's existing setting.

The top button cycles ambient → numbers → night → ambient after setup and
pairing handling. The selected view survives reboot. An unknown clock labels
the observation in UTC and marks age as unknown; after three hours the header
requests a refresh. Setup, pairing and error states use the SDK's status screen. During reconnection,
the saved snapshot remains visible until Muse refreshes.

The page redraws when visible content changes or the age header changes.
`Updated hh:mm` is the observation time; it stays steady as the local clock
advances. Night is selected with the view control. Frames are drawn in strips
with brief pauses between them. Muse images temporarily replace the advisor;
`display.show_animation {}` wakes the dashboard worker to restore it.

## Check locally

```sh
cd esp32
CARD_PREVIEW_DIR=/tmp/card-preview python3 -m unittest tests.test_air_guardian
```

Native-renderer previews: `/tmp/card-preview/outdoor_good.ppm`,
`outdoor_moderate.ppm`, `outdoor_unhealthy.ppm`, `outdoor_night.ppm`.
See [VALIDATION.md](VALIDATION.md) for build results and hardware checks.
