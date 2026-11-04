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

# Kid Board validation

Date: 2026-11-04. Branch: `project/kid-board`. Base commit: `e1bc390`
(`project/care-board`), plus the uncommitted Kid Board implementation.

**Hardware units: 0. Board revision: untested.** Results below are host and build
checks. Device pairing, actual Muse command invocation, physical key/buzzer/RTC,
NVS recovery after power interruption and Spectra 6 refresh await an E1002 test.
E1001 regression evidence here is a build and its existing host tests.

## Tested source and environment

- Source fingerprint: `8de613b656f10bc8991a055b572276a22bfb918869a111b19b380f78bd66ba7c` (SHA-256 of sorted relative source/test paths,
  each followed by a NUL, file bytes, and a NUL; paths listed below).
- ESP-IDF **v6.0.1**, Xtensa ESP32-S3 GCC **15.2.0**; build Python **3.14**
  at `~/.espressif/python_env/idf6.0_py3.14_env`.
- Host: macOS, Python **3.9**, Apple Clang; C harnesses compile with
  `-Wall -Wextra -Werror` and AddressSanitizer/UndefinedBehaviorSanitizer.
- Managed dependencies: cJSON `1.7.19~2`, esp_websocket_client `1.2.3`, led_strip
  `3.0.3`, copied from the existing local SDK checkout. Existing Pillow/font
  sources supplied asset generation; PNG previews use Python's standard library.
- The sandbox denies process enumeration used by the installed IDF component
  manager. For these builds a temporary `/tmp/kid-idf-shim/sitecustomize.py` sets
  its `get_cmake_pid` helper to `os.getppid`, the helper's documented fallback.
  Builds used `PYTHONPATH=/tmp/kid-idf-shim`. Project and system SDK sources retain
  their normal settings. The component registry was unreachable; the existing
  locked local dependencies supplied both builds.
- Build settings have no configured SDK token; device setup follows the project
  README. Validation consists of compilation and host execution.

## Acceptance commands and results

Run from `esp32/`:

```sh
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf6.0_py3.14_env
python3 -m unittest tests.test_kid_board tests.test_care_board tests.test_epaper_cards tests.test_gadget_events
CARD_PREVIEW_DIR=/tmp/card-preview python3 -m unittest tests.test_kid_board
tools/board.sh reterminal-e1002 build
tools/board.sh reterminal-e1001 build
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

| Check | Result |
|---|---|
| Four focused modules | **25 tests passed** |
| Kid Board with previews | **8 tests passed** |
| Current full suite | **776 tests**, 1 failure, 8 errors, 5 skipped |
| Base full suite, same host and cJSON source | **768 tests**, the same 1 failure, 8 errors, 5 skipped |
| Added full-suite failure identities | **0**; all 9 failure/error identities match the base |
| E1002 registration, Kid/events/clock enabled, display/sensor/tunnel present | **5,630 bytes**, within the existing 8,192-byte limit |
| E1001 registration, Care/events/clock enabled | **5,302 bytes**, within the same limit |
| Overlay settings | Every explicit setting applied on both boards; Kid only on E1002, Care only on E1001 |
| Asset regeneration | Repeated generated files are byte-for-byte identical |
| Kid asset selection | ELF contains 20/28/40 px fonts and 64 px Kid icon masks; 60/96 px fonts and 112 px icons are absent |
| Source hygiene | `git diff --check` passed; fixed partition table unchanged |

The host harness covers invalid/duplicate fields, dates/leap years, HH:MM,
identifier/text/numeric limits, ten-item capacity, stable ordering, due/next
selection, future plans, same-date retention and date rollover, remote undo,
Green confirmation and `kid.done` contents, event-queue rejection and debounce,
star goal/reward, NVS round trip/corruption/failure/retry, asynchronous command
ownership and queue saturation, first/idle refresh and coalescing, delayed actual
render starts, render enqueue failure, unknown icon fallback and a 20-star chart.
Logs are checked for IDs/timestamps while personal labels remain event content.

## Build sizes

Both builds began from fresh board-specific directories; final incremental builds
verified the completed source. `check_sizes` reports the signed application:

| Profile | App bytes | Smallest app slot | Free bytes | Free percentage |
|---|---:|---:|---:|---:|
| reTerminal E1002 / Kid Board | `0x191000` = 1,642,496 | `0x200000` = 2,097,152 | `0x6f000` = 454,656 | **21.68%** (`check_sizes`: **22%**) |
| reTerminal E1001 / Care Board | `0x1a1000` = 1,708,032 | `0x200000` = 2,097,152 | `0x5f000` = 389,120 | **18.55%** (`check_sizes`: **19%**) |

The E1002 exceeds the required 15% free space in the existing slot.
`partitions.csv` and the CodeCraft Hub flashing offsets retain their base values.
Kid font/icon mask and metadata accounting is 19,772 bytes; its icons use native
six-colour tinting. Care's existing asset set remains 48,955 bytes.

Build logs: `/tmp/kid-e1002-verified-build.log` and
`/tmp/kid-e1001-verified-build.log`. Focused log:
`/tmp/kid-focused-tests.log`; final preview log:
`/tmp/kid-preview-final-tests.log`; final full log:
`/tmp/kid-full-final-tests.log`; matched base log:
`/tmp/kid-base-matched-tests.log`.

## Preview inspection

Each file is 800×480. PPM and PNG contain only exact black, white, red, yellow,
blue and green RGB values. All four PNGs were visually inspected after the final
layout/icon changes. Morning shows five routine steps; afternoon shows the
replacement piano lesson and Grandpa pickup in a full ten-card layout; done
shows the large green check and the next highlighted step; goal shows five
coloured stars and the earned reward. Homework fits using the smaller text size.

| Preview | PPM | PNG |
|---|---|---|
| `kid_morning` | `/tmp/card-preview/kid_morning.ppm` | `/tmp/card-preview/kid_morning.png` |
| `kid_afternoon_changed` | `/tmp/card-preview/kid_afternoon_changed.ppm` | `/tmp/card-preview/kid_afternoon_changed.png` |
| `kid_done` | `/tmp/card-preview/kid_done.ppm` | `/tmp/card-preview/kid_done.png` |
| `kid_stars_goal` | `/tmp/card-preview/kid_stars_goal.ppm` | `/tmp/card-preview/kid_stars_goal.png` |

## Existing full-suite limitations

The same failure/error identities were reproduced on the base archive and the
completed tree using the same Python/compiler and local cJSON source:

| Existing issue | Result |
|---|---|
| Three `test_coexist_bench.BenchServerTest` HTTP tests | Sandbox denies local socket bind |
| `test_muse_eink_navigation.MuseEinkNavigationTest` | Pinned FreeInk checkout is missing |
| `test_muse_eink_presentation.MuseEinkTextValidationTest` and `MuseEinkWifiOwnershipTest` | Same missing pinned FreeInk source |
| `test_muse_ptt_ui.StopWatchPTT` | Apple Clang treats unused `-no-pie` as an error |
| `test_muse_voice_call.VoicePeCallLoop` | Existing cJSON `sprintf` calls trigger this macOS SDK's deprecation error |
| `test_simulator_avatar_selection.AvatarSelection.test_add_and_remove_without_manual_reconfigure` | Existing avatar selection returns `custom` where the test expects `default` |

Five skips have the same missing inputs on both trees: Python `cryptography`
for pairing transcript vectors, libvpx encoder, libx264, PSA Crypto development
headers/libraries, and pinned `ppk2-api==0.9.2`.

## Hardware follow-up

With one E1002, record its board revision and tested source revision, provision
its SDK token locally, back up the original image and exercise:

1. Pair/reconnect and issue all four Kid commands from Muse; verify complete
   schedule replacement, cancellation/pickup changes, stars and the earned reward.
2. Press Green for each routine step; verify short beep, one `kid.done` per
   accepted press, matching state and the owner's received confirmation.
3. Test a future plan, local midnight, restart/power-cycle NVS restoration,
   RTC recovery and clock/time-zone changes.
4. Measure actual panel redraw start intervals under rapid command/key changes,
   confirm six physical inks and check the visual result on the wall display.
5. Repeat affected Care Board interactions on an E1001 if shared foundations are
   subsequently changed; report physical results separately from these host builds.

## Source fingerprint paths

```text
esp32/devices/sdkconfig.reterminal-e1002
esp32/main/CMakeLists.txt
esp32/main/Kconfig.projbuild
esp32/main/app.c
esp32/main/card_fonts.c
esp32/main/card_icons.c
esp32/main/card_icons.h
esp32/main/kid_board.c
esp32/main/kid_board.h
esp32/main/kid_board_view.c
esp32/main/kid_memory.h
esp32/main/kid_model.c
esp32/main/kid_model.h
esp32/main/kid_store.c
esp32/main/kid_store.h
esp32/main/noise_control.cpp
esp32/tests/kid_board_harness.c
esp32/tests/test_gadget_events.py
esp32/tests/test_kid_board.py
esp32/tools/gen_card_assets.py
```
