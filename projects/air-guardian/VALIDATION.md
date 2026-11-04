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

# Air Guardian validation

Date: 2026-11-04. Source: uncommitted `project/air-guardian` worktree based on
`b2273539aee74ac5e82b2e629d848c1bdb21992a`.
Target profile: SenseCAP Indicator, ESP32-S3. Physical units tested: **0**;
board revision remains unverified. Serial ports and flash were untouched.

Host tools: macOS, Python 3.9.6, Apple Clang 21.0.0. Firmware toolchain:
ESP-IDF v6.0.1, with the specified `idf6.0_py3.14_env` environment.

## Changed files

Paths are relative to the repository root.

| Files | Result |
|---|---|
| `esp32/main/air_guardian.c`, `air_guardian.h` | Queued command worker, sensor snapshots, persistent settings and alert state, event API and display handoff. |
| `esp32/main/air_guardian_core.c`, `air_guardian_core.h` | Seven-day ring, bounded history buckets, validation, status, hysteresis, per-metric cooldown and recovery. |
| `esp32/main/air_guardian_render.c` | Ambient, numbers, night, auto selection and pulsing alert border. |
| `esp32/main/card_rgb565.c`, `card_rgb565.h` | Native RGB565 rectangles, ring, lines, proportional text and generated icons. |
| `esp32/main/app.c`, `noise_control.cpp` | Startup, guarded command dispatch/advertisement and top-button view cycling after pairing/setup handling. |
| `esp32/main/led_status.c` | Serializes dashboard, images and status; status changes queue display work through the existing LED task. |
| `esp32/main/Kconfig.projbuild`, `CMakeLists.txt` | Opt-in application, shared hidden card-assets option and conditional sources. |
| `esp32/devices/sdkconfig.sensecap-indicator` | Enables the application on the Indicator overlay and cites vendor hardware/protocol sources. |
| `esp32/tools/gen_card_assets.py` | Additional air icons and an optional icon-font input path. |
| `esp32/main/card_icons.c`, `card_assets_manifest.json` | Deterministically regenerated masks and updated resource budget. |
| `esp32/tests/test_air_guardian.py`, `air_guardian_harness.c`, `air_guardian_runtime_harness.c` | Real-C host tests with address/undefined-behaviour checks and four native previews. |
| `esp32/tests/test_epaper_cards.py` | Checks that fonts/icons are selected through shared card assets. |
| `projects/air-guardian/README.md`, `README.zh-CN.md`, `SKILL.md`, `VALIDATION.md` | Setup, behaviour, commands, Muse routines, care-context examples, limitations and verification. |

The event foundation, sensor protocol, e-paper renderer, partition tables and
existing hardware pin definitions retain their implementations.

## Host acceptance

From `esp32/`:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
```

The final diagnostic run added `-v` to enumerate skip reasons:

```text
Ran 770 tests in 152.160s
FAILED (failures=1, errors=8, skipped=7)
```

The initial baseline was 757 tests with the **same failure, eight errors and
seven skips**. The remaining failure is
`test_simulator_avatar_selection.AvatarSelection.test_add_and_remove_without_manual_reconfigure`.
The errors are:

| Tests/classes | Cause |
|---|---|
| Three `test_coexist_bench.BenchServerTest` HTTP tests | Sandbox denies local socket binding. |
| `test_muse_eink_navigation.MuseEinkNavigationTest` | Pinned FreeInk checkout absent. |
| `test_muse_eink_presentation.MuseEinkTextValidationTest` and `MuseEinkWifiOwnershipTest` | Pinned FreeInk checkout absent. |
| `test_muse_ptt_ui.StopWatchPTT` | macOS Clang treats unused `-no-pie` as an error. |
| `test_muse_voice_call.VoicePeCallLoop` | Existing cJSON `sprintf` deprecation becomes an error on macOS. |

The seven skipped cases need these inputs:

| Test | Missing input |
|---|---|
| `test_link_pairing_handshake` real-crypto case | `IDF_PATH` or `MBEDTLS_SOURCE_DIR` in the host-test shell. |
| `test_link_pairing_transcript` real vectors | Python `cryptography`. |
| `test_musegadgetstest_vp8_loops` | Host libvpx encoder via pkg-config. |
| `test_musegadgetstest_x264_loops` | Host libx264 via pkg-config. |
| `test_noise_core` PSA case | PSA Crypto headers/libraries. |
| `test_power_test_config` IDF symbol check | `IDF_PATH` in the host-test shell. |
| `test_ppk2_profile` synthetic benchmark | Pinned `ppk2-api==0.9.2`. |

After the final LCD handoff checks, the focused application command passed:

```sh
CARD_PREVIEW_DIR=/tmp/air-preview python3 -m unittest tests.test_air_guardian
```

```text
Ran 13 tests
OK
```

Coverage: minute sampling, seven-day rollover, gaps, min/avg/max/count,
response bounds, all six limits, shared high/low cooldowns, failed-event
queue retries, recovery, missing sensors, all view boundaries, all command
parameters, NVS save/restore, failed settings writes, asynchronous request
ownership/session generation, image/status/dashboard handoff, top-button
ordering, guarded advertisement/dispatch and registration size.

The directory contains `air_ambient.ppm`, `air_numbers.ppm`, `air_night.ppm`
and `air_alert.ppm`, each a native 480×480 RGB preview. All four were visually
inspected after conversion to PNG. The preview generator is the production
renderer. Physical panel brightness, timing and colour remain hardware checks.

Registration with Air Guardian enabled: **4642 bytes**, below 8192. The tested
full seven-day history response: **2752 bytes**; it is capped at 24 points
with bounded sensor ranges. Shared assets: **138762 / 163840 bytes**.
Repeated regeneration produced identical hashes. Local documentation links
and `git diff --check` passed.

## Firmware build boundary

The specified fresh build was attempted once from `esp32/`:

```sh
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf6.0_py3.14_env
. ~/esp/esp-idf/export.sh
idf.py -B build-codex-indicator -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-codex-indicator/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.sensecap-indicator" build
```

Configuration stopped inside the IDF component manager's `psutil` process
lookup with `PermissionError: [Errno 1] Operation not permitted`, originating
from macOS `sysctl()`. Build attempts stopped at that sandbox boundary.
No firmware binary, applied-overlay verification or application-size result
is available from this run.

## Hardware and build verification

On an unrestricted host, build the Indicator from a fresh directory, check
the overlay and application slot size, and build an untouched LCD board
(such as ideaspark) plus an e-paper card profile for the shared integration.
On D1S/D1Pro with stock RP2040 firmware and Grove AHT20, record hardware
revision and unit count, then check:

1. Pairing confirmation, setup window, five-second reset and reconnect;
   original Muse status during setup, errors and disconnection.
2. All four views, top-button cycling and reboot persistence, actual local
   22:00/07:00 transitions, night visibility and border pulse.
3. Fresh/missing sensors, minute history, seven-day PSRAM use, all six
   crossings, cooldown and recovery, including event arrival in Muse chat.
4. Images through `display.draw_url` and `display.draw_file`, followed by
   `display.show_animation`, and disconnection during an image transfer.

History uses approximately 315 KiB of PSRAM and the drawing buffer 450 KiB,
in addition to the existing LCD framebuffer. Saved room/limits/view and last
alert state survive reboot; history and the foundation's pending event queue
start over in RAM. The project has text notifications and visual alerts.
