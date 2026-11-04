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

# Fridge Notes verification

Tested on 2026-11-04, branch `project/fridge-notes`, base commit `b227353` plus
this uncommitted patch. Target: Seeed reTerminal Sticky, full Muse/FreeInk UI.
Hardware units exercised: **1** (MAC e8:3d:c1:fc:79:74); board revision: factory Sticky, CH343 console. ESP-IDF checkout:
**v6.0.1**; ESP32-S3 compiler: **Xtensa GCC 15.2.0**; host compiler: Apple Clang.
FreeInk source pin: `2c047df7792cedece1191503793f18f5d8f1c695`, supplied by
`esp32/managed_components/freeink-sdk`.

## Changed files

| Files | Result |
|---|---|
| `esp32/main/sticky_notes.c`, `sticky_notes.h`, `sticky_notes_model.c`, `sticky_notes_model.h` | Bounded model, command validation, NVS storage, asynchronous VM mirror and touch events |
| `esp32/main/app.c`, `noise_control.cpp` | Startup, gated dispatch and command registration |
| `esp32/main/CMakeLists.txt`, `Kconfig.projbuild`, `esp32/devices/sdkconfig.reterminal-sticky` | Build integration and Sticky-only feature selection |
| `esp32/components/muse/CMakeLists.txt`, `eink/muse_eink_ui.cpp`, `eink/muse_eink_notes.cpp`, `eink/muse_eink_notes.h` | FreeInk list page, row interactions, navigation and reply return |
| `esp32/tests/sticky_notes_model_harness.c`, `sticky_notes_service_harness.c`, `sticky_notes_eink_harness.cpp`, `test_sticky_notes.py`, `test_sticky_notes_eink.py` | Production-code host checks |
| `projects/fridge-notes/README.md`, `README.zh-CN.md`, `SKILL.md`, `VALIDATION.md`, `esp32/devices/README.md` | Setup, family usage, Muse skill, limits and verification |

## Automated checks

From `esp32/`:

```sh
FREEINK_SOURCE_DIR=$PWD/managed_components/freeink-sdk python3 -m unittest discover -s tests -p 'test_sticky_notes*.py' -v
```

**15 tests passed**, with address and undefined-behaviour sanitizers enabled.
They cover six commands and validation, atomic rejection, item/list/text limits,
allocation failures, NVS save/load and storage failure, a full command queue,
JSON reconstruction, UTF-8 chunk boundaries, all required file-write booleans,
three-second debounce, touch-event coalescing and its ten-second burst bound,
offline retry, non-2xx replies, send failure, timeout and late callbacks.
A multi-chunk upload retains its snapshot while a newer edit stays pending,
then uploads the newer revision. FreeInk tests route actual production hit
regions for toggles, tabs, swipes, twenty-row paging, clear done, stale touches
after rows move, return after a voice reply and an open Settings editor.

The production registration builder and command dispatch block are executed
with the option both off and on. The Sticky registration measures **3694 bytes
off / 5438 bytes on**, including events, clock, display, sensors, tunnel and OTA;
both fit the 8192-byte limit. Other overlays keep the option off.

```sh
FREEINK_SOURCE_DIR=$PWD/managed_components/freeink-sdk python3 -m unittest discover -s tests -p 'test_*.py'
```

Final full-suite run: **814 tests; `FAILED (failures=1, errors=5, skipped=7)`**.
The same failure and five errors occurred before this patch:

| Existing failure/error | Evidence |
|---|---|
| `test_simulator_avatar_selection.AvatarSelection` | The generated avatar choice reports `default` instead of the expected `custom` |
| Three `test_coexist_bench.BenchServerTest` cases | Sandbox denies binding the local HTTP server: `PermissionError: [Errno 1] Operation not permitted` |
| `test_muse_ptt_ui.StopWatchPTT` setup | Apple Clang rejects unused `-no-pie` under `-Werror` |
| `test_muse_voice_call.VoicePeCallLoop` setup | macOS cJSON `sprintf` deprecation warnings are treated as errors |

`test_simulator_avatar_selection` passed in a separate full verbose run,
which ended with `FAILED (errors=5, skipped=7)`. The final acceptance run
reproduced its existing failure; its result is intermittent in this environment.

The seven skipped cases and missing inputs are:

| Case/module | Missing input |
|---|---|
| `test_link_pairing_handshake` real-crypto case | `IDF_PATH` or `MBEDTLS_SOURCE_DIR` in the host-test environment |
| `test_link_pairing_transcript` real-crypto vectors | Python `cryptography` |
| `test_musegadgetstest_vp8_loops` | Host `libvpx` encoder via `pkg-config` |
| `test_musegadgetstest_x264_loops` | Host `libx264` via `pkg-config` |
| `test_noise_core` PSA Crypto compile | PSA Crypto development headers/libraries |
| `test_power_test_config` IDF symbol check | `IDF_PATH` in the host-test environment |
| `test_ppk2_profile` synthetic benchmark | Pinned `ppk2-api==0.9.2` |

Pinned FreeInk navigation and text tests ran; they were not skipped.
`git diff --check` passed. Project documentation's relative file links resolve.

## Firmware compilation

The standard command was attempted:

```sh
tools/board.sh reterminal-sticky build
```

**A complete firmware build was not possible in this sandbox.** ESP-IDF's
component manager failed during CMake configuration because macOS process
inspection through `psutil` / `sysctl` is denied. A fresh cached-dependency
configure with `IDF_COMPONENT_MANAGER=0` also stopped at dependency resolution
(`esp_lcd_touch` alias unavailable with that manager disabled).

A separate compilation check used the existing Sticky compilation database and
ESP32-S3 compiler, with a temporary generated-config header enabling Notes,
events and clock and the Notes include directory. These eight translation units
compiled successfully:

- `main/sticky_notes.c`, `main/sticky_notes_model.c`
- `main/app.c`, `main/noise_control.cpp`
- `main/gadget_clock.c`, `main/gadget_events.c`
- `components/muse/eink/muse_eink_notes.cpp`, `muse_eink_ui.cpp`

This checks source compilation. Linking, overlay propagation through a fresh
configure, application size and partition headroom still require a complete
build. Firmware images and size figures are unavailable for this patch.

## Reviewer build and hardware checks

1. In an ESP-IDF v6.0.1 environment with normal component-manager access, run
   `tools/board.sh reterminal-sticky build` from a fresh build directory, set the
   private SDK token through menuconfig and rebuild. Check `MUSE_STICKY_NOTES`,
   `HOMEHUB_GADGET_EVENTS` and `HOMEHUB_GADGET_CLOCK` in the generated config,
   the link result and the application-size/partition-headroom line. Also build
   Sticky with Notes disabled and an unaffected full-UI board.
2. On a backed-up Sticky, check pairing and reconnect, Wi-Fi keyboard save/cancel,
   AI-button recording, spoken requests becoming the expected `notes.add` calls,
   short reply display and return to Notes. Exercise all four lists and twenty
   rows, fast partial checkbox updates, gray strike-through, clear done,
   rapid touches after clearing, face/Settings navigation, Up/Down wake and
   battery sleep. Record the board revision and unit count.
3. Check list restoration after reboot, actual NVS capacity near the limits,
   successful VM JSON upload and local sync time, edits during upload,
   reconnect after offline changes and reads through Muse while the board
   sleeps. Verify touch summaries appear once per burst in the user's Muse
   chat, including completion, undo and clear. Report results against the
   final reviewed revision and measure any battery impact of retries.

The shared gadget event and clock modules are consumed through their public
APIs; their source implementations are unchanged by this patch. Existing board
validation remains in [the device guide](../../esp32/devices/README.md).

## On-device (2026-11-04)

Flashed Fridge Notes after a full 32 MB factory backup
(`backups/reterminal-sticky-e83dc1fc7974-factory.bin` in the challenge repo).
Device reported Online with Wi-Fi connected. Through `tools/muse/chat.py`:

1. Muse created Shopping, Chores, To-do and Notes, then added Eggs and Milk to Shopping.
2. Muse read the shopping list and reported both items unchecked.
3. Muse marked Eggs done; Milk remained open.

Touch toggles, AI-button voice notes, VM mirror contents and sleep/wake were
not exercised in this pass. Battery was about 11–13 % during the checks.
