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

# Outdoor Air validation

Date: 2026-11-04. Source: uncommitted `project/air-guardian` worktree on
`dbfb451`, shared foundation `b227353`. Target: **SenseCAP Indicator D1 only**,
Link-only profile, ESP32-S3, 3.95-inch LCD. Physical units tested: **0**;
board revision awaits hardware verification. Serial ports and flash untouched.

D1S and D1 Pro are outside this application's scope. D1 has no onboard CO2/tVOC
sensors. Outdoor Air uses Muse-pushed outdoor snapshots and has no indoor
sensor, Grove AHT20 or RP2040 sensor UART dependency. The board overlay's
sensor option retains its existing value; the app skips the UART reader and
indoor command advertisement. Host runtime links without any sensor implementation.

Host tools: macOS, Python 3.9.6, Apple Clang 21.0.0. Firmware toolchain:
ESP-IDF **v6.0.1**, `idf6.0_py3.14_env`.

## Files and command surface

| Files | Current behaviour |
|---|---|
| `esp32/main/air_guardian_core.c`, `air_guardian_core.h` | Complete outdoor snapshot validation, observation age, versioned atomic persistence and three views. |
| `esp32/main/air_guardian.c`, `air_guardian.h` | Queued updates, NVS restore and LCD rendering. |
| `esp32/main/air_guardian_render.c` | Category band, large metric, short advice, place and update time. |
| `esp32/main/app.c`, `noise_control.cpp`, `led_status.c` | Three guarded commands, button cycling, sensor-reader skip and saved display during reconnection. |
| `esp32/main/Kconfig.projbuild`, `devices/sdkconfig.sensecap-indicator` | D1 LCD app gate, independent from the sensor option; enabled only in Indicator overlay. |
| `esp32/tests/test_air_guardian.py`, `air_guardian_harness.c`, `air_guardian_runtime_harness.c` | Core, persistence, worker, rendering and integration tests. |
| Project `README.md`, `README.zh-CN.md`, `SKILL.md`, `VALIDATION.md` | D1-only Muse outdoor workflow, commands and verification. |

Commands: `air.update`, `air.status`, `air.set_view`. Snapshots and selected
view are stored together as `outdoor_air` in NVS. Optional metrics remain
absent when omitted/null; the last valid snapshot survives reboot. This app
emits no category events. The shared `clock.set` command supplies local time.

## Host checks

From `esp32/`:

```sh
CARD_PREVIEW_DIR=/tmp/card-preview python3 -m unittest tests.test_air_guardian
python3 -m unittest discover -s tests -p 'test_*.py'
```

The focused tests cover all US AQI category edges, PM2.5-only and missing
metrics, range/type/text/duplicate/unknown-field validation, observation order
and future bounds, failed-save atomicity, versioned restore/corrupt records,
three view choices and button wraparound, age with and without a known clock,
three-hour refresh threshold, session-bound asynchronous replies, allocation
and queue rejection, reconnect/image/status ownership, real numeric ink on
screen and canvas bounds across every category/view. C harnesses run with
address and undefined-behaviour sanitizers.

Focused result: **12 tests passed**, with sanitizers enabled. Full registration
is **4255 bytes** with the app enabled and **2713 bytes** without it, both
below the 8192-byte registration bound. Registration checks confirm the three
outdoor commands and omit indoor-only commands when enabled.

Four production-renderer previews are under `/tmp/card-preview/`:
`outdoor_good.ppm`, `outdoor_moderate.ppm`, `outdoor_unhealthy.ppm`,
`outdoor_night.ppm` (480×480). PNG companions use the same basename. All four
were visually inspected; the numbers preview also has pixel-level numeric-ink
assertions.

Baseline full suite before edits: **770 tests; 1 failure, 8 errors, 5 skips**.
The baseline failure is simulator avatar-selection reconfiguration. Errors:
three sandbox-denied local socket tests; three missing FreeInk test setups;
StopWatch PTT unused `-no-pie` flag; Voice PE cJSON deprecation-as-error.
The five skips need cryptography, libvpx, libx264, PSA crypto and pinned ppk2-api.

Final full-suite result: **769 tests in 159.082 seconds; 1 failure, 8 errors,
5 skips**, with the exact same failure/error test names as the baseline.
**No new full-suite failures or errors.** The suite has one fewer case because
the indoor history/alarm cases were replaced with outdoor snapshot cases.
Focused tests: **12 passed** in 4.011 seconds.


## Firmware checks

Acceptance build:

```sh
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf6.0_py3.14_env
tools/board.sh sensecap-indicator build
```

Standard helper result: **PASS**, using `build-sensecap-indicator`.
`check_sizes`: app **0x1b1000 bytes**; smallest app slot **0x200000**;
**0x4f000 bytes (15%) free**. Bootloader: **0x5850**, **66% free**.
The applied config has the Indicator ST7701 backend, PSRAM, app, clock and
card-assets flags enabled; the existing sensor flag remains enabled.

A fresh normal configure in `build-outdoor-air-d1` hits the existing macOS
sandbox boundary: IDF component manager calls `psutil` to inspect parent
processes; `sysctl` returns `PermissionError`. The standard helper can use the
already-configured Indicator build. An untouched ideaspark build encounters
the same component-manager sandbox error. An additional fresh local-dependency
attempt with `IDF_COMPONENT_MANAGER=0` and the existing managed sources compiles
the application, then stops at the camera component's manifest-supplied cJSON
requirement; it is not counted as a successful fresh build. Complete fresh-board
validation remains pending on a host where the component manager can run.
Build-only images have no configured SDK
token; pairing/cloud operation therefore remains a separate hardware check.

## Hardware checks still required

On the owner's D1, record board revision and unit count, then exercise:

1. Setup, pairing confirmation, reset controls and reconnect; push outdoor
   observations with all indoor readings absent, then verify saved data through
   reboot and temporary loss of connectivity.
2. Complete `air.update` → command result → LCD → `air.status` flow, invalid
   update retention, source observation time, local offset and unknown/stale age.
3. All three views, top-button wraparound, physical colour/brightness and night
   readability, Muse images and return to advisor, and setup/error status.

Host previews verify pixels and layout. Physical LCD timing, brightness,
button interaction, flash persistence and Muse tool/routine behaviour await
hardware validation. The render buffer is 450 KiB in PSRAM; snapshot state
uses bounded fixed strings and NVS, with no history allocation.
