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

## On the device — 2026-11-05

One reTerminal E1002 (ESP32-S3, CH340 console), paired with Muse over home Wi-Fi,
with the Kid Board `SKILL.md` pasted into the owner's Muse chat.

| Check | Result |
|---|---|
| “Plan Mia's day: brush teeth in 5 minutes, dress in 10, breakfast in 15; 2 stars, goal 5, reward park” | Muse called `clock.set`, `kid.set_schedule`, `kid.set_stars`; the six-colour page showed the timeline, stars and reward; layout checked by the owner |
| Green pressed at the brushing step | Beep, row ticked, `kid.done` posted at once; Muse told the owner |
| Refresh before the scheduling fix | A full 26 s Spectra 6 refresh every 60 s with no change |
| Refresh after the fix | One refresh on restore at boot, none in the next 4.5 minutes; `epd` stack 2136 bytes free |

`tools/board.sh reterminal-e1002 build`: app `0x191000` bytes in the fixed
`0x200000` slot, 22 % free. Still to check on the device: a redraw at the next
item's start time, the reward page at the star goal, midnight rollover and
restoring the plan after a power cycle.

## Refresh scheduling — 2026-11-05

Tested revision: `e35aa88` plus the uncommitted refresh-scheduling patch.
Source/test fingerprint: `12732e4116fc36c5376acc1d3195febaca5b3b2a001c0da6ef05b6ee3f570a53` (the NUL-delimited path/content algorithm
used below, over the changed `esp32/` files listed at the end of this section).

### Hardware evidence and stack budget

Owner-supplied evidence covers **one paired E1002**, with its Muse skill installed.
Muse successfully invoked `clock.set`, `kid.set_schedule` and `kid.set_stars`.
The supplied log records 26,360 ms full refreshes near 435, 495, 555, 615, 676 and
736 seconds, with an `epd` stack high-water mark of **872 bytes free**. Board
revision was not supplied. These measurements describe the previous firmware.

**This patch has 0 hardware units tested.** Its physical refresh intervals and
post-change stack high-water mark await a device run.

Kid Board renders on the `epd` task. Its stack is now **4,096 bytes**, adding
1,024 bytes; other profiles retain 3,072 bytes. GCC 15.2.0 `-fstack-usage` checks
against `e35aa88` report the render callback unchanged at 48 bytes, the view
reduced from 256 to 240 bytes, and current-item selection unchanged at 48 bytes.
On the measured rendering path this projects about **1,912 bytes free** against
the **1,536-byte** target. This is a static budget estimate; the device's
`link.stack` measurement establishes the actual remaining stack.

### Current behavior

The header shows the child, weekday/date and plan date. The highlighted item is
the most recently due unfinished item, otherwise the next item today; equal-time
steps retain input order. Green confirms that highlighted item. The worker
calculates the next actual highlight change or local midnight and sleeps until
that boundary or an external command/key/clock notification. Schedule, stars,
note and completion edits enter the same existing 60-second coalescing window.
A rendered snapshot's model revision, date and selected item account for updates
already included in queued drawing. Identical commands preserve the screen.

Each redraw request emits one `link.kid` line containing a fixed reason and UTC
time. Reasons include `kid.set_schedule`, `kid.set_stars`, `kid.mark`, `key`,
`current`, `date` and `restore`; labels and parent notes remain outside these logs.

### Host and build checks

Run from `esp32/`:

```sh
python3 -m unittest tests.test_kid_board tests.test_gadget_events tests.test_care_board
CARD_PREVIEW_DIR=/tmp/card-preview python3 -m unittest tests.test_kid_board
python3 -m unittest tests.test_epaper_cards tests.test_link_epaper_status
```

- Acceptance modules: **25 tests passed**; Kid Board preview run: **11 passed**.
- Including shared e-paper coverage: **48 passed**. Both Kid-enabled and generic
  task creation paths verify their configured stack sizes.
- Full suite: **780 tests**, 1 failure, 8 errors, 7 skipped. All nine failure/error
  identities match the initial validation's existing environment/fixture issues
  listed below; the log is `/tmp/kid-refresh-full-tests.log`.
- One hour from 07:00 through 08:00, with seven items ten minutes apart, produces
  **7 redraw requests/refreshes**: one initial draw and six current-item changes.
  Independent clock notifications every minute leave the other minutes unchanged.
  Worker waits match actual item deadlines, including the final wait to midnight.
- A star-only page receives an hour of minute notifications with **0 extra
  requests/refreshes**; rendered pixels are byte-identical. Identical star and
  state commands also preserve redraw counts and NVS writes.
- A restored, completed schedule crosses local midnight with **1 additional
  request/refresh**, resetting done flags once. Tests also cover time-zone offsets,
  future plans, completed plans, delayed render starts and content coalescing.
- Four true-six-colour PPM/PNG previews were regenerated at the paths below and
  visually inspected. The header displays the weekday/date; goal progress and
  the current highlight remain legible.
- E1002 and E1001 builds pass with ESP-IDF v6.0.1 / Xtensa GCC 15.2.0. Signed
  sizes remain `0x191000` (E1002, **21.68% free**, rounded 22%) and `0x1a1000`
  (E1001, **18.55% free**, rounded 19%). The fixed partition table is unchanged.
  These builds use the same local component-manager fallback described below.
- Updated E1002 command registration: **5,628 bytes**; E1001: **5,302 bytes**.

Logs: `/tmp/kid-refresh-acceptance.log`, `/tmp/kid-refresh-previews.log`,
`/tmp/kid-refresh-final-tests.log`, `/tmp/kid-refresh-e1002-build.log` and
`/tmp/kid-refresh-e1001-build.log`. Static frame reports reside in
`/tmp/kid-refresh-stack/`.

Fingerprint paths:

```text
esp32/main/epaper_status.c
esp32/main/kid_board.c
esp32/main/kid_board_view.c
esp32/main/kid_model.c
esp32/main/kid_model.h
esp32/main/noise_control.cpp
esp32/tests/epaper_cards_status_harness.c
esp32/tests/kid_board_harness.c
esp32/tests/test_epaper_cards.py
esp32/tests/test_kid_board.py
```

## Initial implementation record — 2026-11-04

The remaining sections preserve the initial implementation's validation results.

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
its SDK token locally and exercise:

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
