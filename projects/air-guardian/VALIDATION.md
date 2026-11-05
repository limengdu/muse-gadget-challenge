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

Date: 2026-11-05. Source: uncommitted `project/air-guardian` worktree on
`7dd55c2`, shared foundation `b227353`. Target: **SenseCAP Indicator D1 only**,
Link-only profile, ESP32-S3, 3.95-inch LCD. Owner-reported hardware finding:
**1 unit**; board revision unspecified. The final revision (event/strip
redraw, 20-line RGB bounce buffers) was flashed and checked on that unit; see
[Final revision on the device](#final-revision-on-the-device-one-d1).

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

Initial Outdoor Air result: **12 tests passed**, with sanitizers enabled. Full registration
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
The owner verified pairing and Muse commands after the RAM change, as below.
The current redraw revision awaits a device retest.

## D1 pairing memory finding and revision

On one freshly booted D1 at `7dd55c2`, the owner observed BLE connection,
Wi-Fi `got IP`, and successful VM lookup. Starting the VM session then logged
`failed to start noise control task` and pairing ended at stage `vm` with
`auth_failed`. The app showed "cannot connect". The session requires a
contiguous 12,288-byte internal stack. The boot-time stack reserve applies to
Muse UI builds with PSRAM; this Indicator profile is Link-only.

Owner-provided heartbeat values (KiB, rounded by the firmware):

| Stage | Internal free / largest | Internal minimum free | DMA free / largest |
|---|---:|---:|---:|
| Boot, unpaired | 24 / 17 | 24 | 17 / 17 |
| After failed pairing | 26 / 8 | 6 | 20 / 8 |
| Paired after task-stack change, LCD staging internal | 58 / 46 | Not recorded | 54 / 46 |

On the same unit, pairing now succeeds and Muse called `air.status`,
`clock.set` and `air.update`, pushing an outdoor snapshot. Connected DMA free
is 54 KiB, above the 22 KiB target and Link's 16 KiB send margin; the largest
internal block is 46 KiB. A fresh unpaired-boot measurement remains to be
recorded separately.

The 8 KiB largest block at session creation is smaller than the 12 KiB stack.
The project now places `gadget_events` and `outdoor_air` stacks in PSRAM.
`gadget_events` uses the existing PSRAM-initialization check for both its
receive buffers and task stack, including the Watcher profile; boards with
unavailable PSRAM use the internal path. An external allocation failure is
reported to the caller. Packed queued/history events also use this allocation
policy, so retained events consume PSRAM on the D1.

Outdoor snapshot serialization and rendering run on the PSRAM worker. Each
save runs `config_set_str()` on a temporary `air_save` task with a 4,096-byte
internal stack. The worker waits for the write result before accepting the
new snapshot/view or replying to the originating session. Allocation and NVS
failure retain the previous state. Restore runs during initialization on the
caller's internal stack. `air_save` logs its stack high-water mark once per
save, and `outdoor_air` logs once after its first render through `link.stack`.
The 4 KiB save budget is provisional: measure the D1 NVS page-erase path and
set its final budget from peak usage plus margin; the source tracks this TODO.

The ST7701 bar and animation/image staging buffers are CPU-copy sources in
internal RAM, matching the owner's working hardware configuration. The pair
of DMA bounce buffers (19,200 bytes combined) retains its internal allocation.
The shared Link task, reserve, transport and send margin are unchanged.

Source allocation sizes and before/after linker-map measurements:

| Allocation | Internal before (bytes) | Internal after (bytes) |
|---|---:|---:|
| `outdoor_air` resident stack | 8,192 | 0 (8,192 in PSRAM) |
| `gadget_events` resident stack | 6,144 | 0 (6,144 in PSRAM) |
| `air_save` additional stack | 0 (write used `outdoor_air`) | 4,096 during each save |
| LCD bar staging (`480 × 24 × 2`) | 23,040 | 23,040 |
| LCD animation/image staging (`56 × 5 × 8 × 5 × 2`) | 22,400 | 22,400 |
| `air_guardian.c` static `.bss` symbols | 168 | 169 |
| `gadget_events.c` static `.bss` symbols | 847 | 847 |
| `led_status.c` static `.bss` symbols | 94 | 94 |

The resident internal-stack reduction is **14,336 bytes (14 KiB)**, excluding
allocator overhead and task control blocks. These three files' static `.bss`
symbols total **1,109 bytes before / 1,110 bytes after**; the display wake flag
adds one byte.
The active D1 config has `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`
disabled, so external working buffers use explicit PSRAM heap allocation. The existing
450 KiB Outdoor Air render buffer and event receive/reply buffers already
use PSRAM. Variable-size event allocations move out of internal RAM as well;
their size depends on the retained events.

RAM-change host acceptance:

```sh
python3 -m unittest tests.test_air_guardian tests.test_gadget_events
```

**20 tests passed** with address/undefined-behaviour sanitizers. Coverage
includes PSRAM and internal task creation, unavailable PSRAM, Watcher flags,
task/event allocation failures, deferred NVS execution on the internal task,
failed-save retention, session replies and internal LCD staging. This
checkout has no `tests.test_home_eyes` module; the two-module fallback passes.
The Indicator helper build passes with ESP-IDF **v6.0.1**:
app **0x1b1000**, slot **0x200000**, **0x4f000 (15%) free**;
bootloader **0x5850**, **66% free**. The applied config enables PSRAM,
external task stacks, Outdoor Air, ST7701 and gadget events.

Remaining RAM checks: on a fresh unpaired boot record largest internal free
block **at least 24 KiB**; exercise update/view persistence, including an NVS
page erase, and retain the `air_save` high-water result. Repeat connected
heap measurements with the final redraw revision.

## RGB redraw finding and revision

After pairing and the Muse-pushed snapshot, the owner observed a moving
yellow line at the top and short white dashes in the middle of the display.
The old worker rendered a complete PSRAM canvas and copied **460,800 bytes**
to the PSRAM RGB framebuffer every five seconds, including unchanged pages.
Those write bursts compete with the RGB panel's PSRAM reads for DMA bounce
refills. Raising only the redraw interval to 60 seconds removed the artefacts
on the owner's unit. That diagnostic establishes the bandwidth-sensitive
trigger; the event/strip revision below still awaits device validation.

The current worker builds a small key from the actual displayed strings,
rounded values, category colour, selected view and age-header state. It
renders only when that key changes or the display returns from an image or
setup/status screen. Read-only commands, identical snapshots, identical view
selections, hidden metrics and changes below the displayed numeric precision
retain the existing frame. Failed updates retain both data and pixels.

The queue timeout is computed from the next visible age boundary: a future
observation becoming current, or observation time plus `AIR_STALE_S` (three
hours). Otherwise the task waits for a command, clock-change callback or
display handoff. Clock synchronization/offset changes wake it to compare the
visible result. `Updated hh:mm` is the observation time; this page has no live
date/minute field, and night is a manually selected view. Minute and midnight
rollovers therefore leave the frame intact. Display wake state survives a
full request queue.

Each full dashboard frame is submitted as **21 horizontal strips**, at most
**23 rows / 22,080 bytes** per strip, copied through the existing internal
`s_anim_buf`. There is a **one-tick delay** between strips (20 yields per full
frame). The display lock protects the staging buffer and ownership throughout
the frame; a setup/status transition stops the remaining strips. Dashboard
bars/dot suppression and Muse-image ownership retain their existing rules.
`display.show_animation` and a disconnected image wake the dashboard directly.
Each successful dashboard redraw logs one `link.air` line containing only
`redraw reason=initial|snapshot|view|age|clock|display`.

Current acceptance commands, from `esp32/`:

```sh
python3 -m unittest tests.test_air_guardian tests.test_gadget_events
CARD_PREVIEW_DIR=/tmp/card-preview python3 -m unittest tests.test_air_guardian
tools/board.sh sensecap-indicator build
```

Combined host result: **20 tests passed**; preview-command result:
**14 tests passed**, with address/undefined-behaviour
sanitizers. A continuously running worker is exercised with a simulated
hour inside its computed three-hour wait and no extra redraw, followed by
one redraw for each visible snapshot, view change and stale threshold.
The scenario also covers future/unknown age, clock offset, midnight/minute
rollover, no-op/failed requests, image hiding/resume, a rejected display wake
with a pending request, and numeric rounding.
The LCD harness checks every strip's pixel contents, final partial strip,
yield count, ownership lock, image/status handoffs and interrupted/failed draws.
Native preview hashes remain unchanged.

The ESP-IDF **v6.0.1** Indicator build passes: app **0x1b1000**, slot
**0x200000**, **0x4f000 (15%) free**; bootloader **0x5850**, **66% free**.
Remaining hardware checks are artefact-free snapshot/view/age transitions,
image return, setup/error/reconnect, and the heap/NVS checks above on the
final event/strip revision. The owner-confirmed 60-second experiment and
pairing results are separate from these pending checks.

## Final revision on the device (one D1)

The final revision keeps the event/strip redraw, draw buffers in internal RAM and
**RGB bounce buffers of 20 lines** (`LCD_BOUNCE_LINES`, was 10). With 10-line
bounce buffers the page still showed occasional shifted lines (the yellow
category stripe at the top changing length, short white dashes) even with no
redraw in progress: the panel's refills from the PSRAM frame buffer fell behind
while PSRAM was busy. With 20 lines the owner saw no artefacts.

| Check | Result |
|---|---|
| Pairing from a fresh boot | Succeeds; connected heap `int=42K/34K`, `dma=38K/34K` |
| Muse commands | `air.status`, `clock.set`, `air.update` answered; snapshot shown |
| Redraws in 3 minutes after boot | Two: `display` at boot, `age` after the clock synchronised |
| Panel artefacts | None with 20-line bounce buffers |

`tools/board.sh sensecap-indicator build`: app `0x1b1000` bytes in the fixed
`0x200000` slot, 15 % free.

## CodeCraft Hub draft flash (2026-11-05)

The uploaded release `muse-gadget-outdoor-air-81cdb80.bin` (token-free, built from `81cdb80`) was flashed from the draft page of Hub app 13472 onto one SenseCAP Indicator D1. After a reset the board started normally and the owner's device checks passed. Hub writes the application image only, so the token and pairing already stored on the board carried over. On the first Hub flash the SDK token was also sent over USB serial (`>sdk_token=…`, answered `@sdk_token ok`). The draft page showed no reset or token prompt ([sensecraft-cli#6](https://github.com/Seeed-Solution/sensecraft-cli/issues/6)).

## Hardware checks still required

On the owner's D1, record board revision and unit count, then exercise:

1. Setup, pairing confirmation, reset controls and reconnect; push outdoor
   observations with all indoor readings absent, then verify saved data through
   reboot and temporary loss of connectivity.
2. Complete `air.update` → command result → LCD → `air.status` flow, invalid
   update retention, source observation time, local offset and unknown/stale age.
3. All three views, top-button wraparound, physical colour/brightness and night
   readability, Muse images and return to advisor, and setup/error status.

Host previews verify pixels and layout. The owner observed pairing and the
three Muse commands above. Final-revision LCD timing, brightness, button
interaction, flash persistence and complete routine behaviour await hardware
validation. The render buffer is 450 KiB in PSRAM; snapshot state
uses bounded fixed strings and NVS, with no history allocation.
