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

# Door Sign verification record

Validated on 2026-11-04 in branch `project/door-sign`, based on `b227353`, with
uncommitted changes. Hardware target: Seeed XIAO ESP32-S3 1.47-inch Touch Display.
Hardware revision is unspecified; physical units exercised in this run: **0**.

## Implementation map

| Files | Responsibility |
|---|---|
| `esp32/main/door_sign_model.c`, `.h` | State defaults, bounded parameter parser, absolute deadlines, versioned persistence encoding and tap cooldown |
| `esp32/main/door_sign.c`, `.h` | Background NVS writes, command results, snapshots, reply and expiry events |
| `esp32/components/muse/muse_door_sign.c`, `.h`, `muse_door_sign_view.h` | LVGL sign, reply buttons, countdown, confirmation and shared state contract |
| `esp32/components/muse/muse_ui.c` | Home-tile integration, conversation visibility and wake-to-sign behaviour |
| `esp32/main/app.c`, `noise_control.cpp` | Startup, dispatch and command advertisement behind the same option |
| `esp32/main/CMakeLists.txt`, `esp32/components/muse/CMakeLists.txt`, `esp32/components/muse/Kconfig`, `esp32/devices/sdkconfig.muse-seeed-xiao-s3-147` | Conditional sources and XIAO-only default enablement |
| `esp32/tests/test_door_sign.py`, `door_sign_harness.c` | Production model/worker harness, option gating and full registration-size checks |
| `esp32/simulator/CMakeLists.txt`, `include/sim_board.h`, `src/sim_board.c`, `tests/door_sign_harness.c`, `tests/test_door_sign.py` | Full production UI tests with configurable screen geometry |
| `esp32/simulator/tests/screenshots/door-sign/*.png` | Twelve checked-in reference captures for three geometries |
| `projects/door-sign/README.md`, `README.zh-CN.md`, `SKILL.md` | Setup, command contract, family interactions and Muse event/routine instructions |

## Design

The session task validates inputs and queues work without waiting for NVS or the
UI. One worker serializes durable sign changes; the view reads a short snapshot.
A screen revision identifies each tap, and a ten-second monotonic cooldown spans
all buttons. Confirmation appears after `gadget_events_post()` accepts an event.
The public event and clock APIs are used as supplied.

Local `HH:MM` resolves to the next occurrence and is saved as an absolute epoch.
A synchronized clock processes overdue signs after reboot. Expiry saves the free
state before posting its notice. An idle sign worker waits on its queue; a pending
deadline or expiry notice adds a one-second timeout. The LVGL frame callback
merges unchanged content and updates the countdown once per second. Existing
LVGL fonts and icons supply the artwork.

## Tools

- macOS 26.6.2, Apple Clang 21.0.0.
- Python 3.9.6, CMake 4.3.3, Ninja; simulator uses the existing LVGL 9.5.0 checkout.
- ESP-IDF checkout `/Users/mengdu/esp/esp-idf`: `v6.0.1`.
- Host model/worker tests compile with AddressSanitizer and UndefinedBehaviorSanitizer.
- Linux and physical-device runs remain for review.

## Host tests

From `esp32/`:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
python3 -m unittest discover -s tests -p 'test_door_sign.py' -v
```

Before edits: **757 tests; 1 failure, 8 errors, 7 skipped**.
Final: **760 tests; 1 failure, 8 errors, 7 skipped**, with the same failure and
error categories as the baseline. Exact summary: `Ran 760 tests in 158.320s` and
`FAILED (failures=1, errors=8, skipped=7)`. The new feature adds three passing tests.

The three Door Sign tests pass. The compiled harness covers all state defaults,
invalid fields and types, UTF-8 and lengths, custom colour, duplicate replies,
clock validity, explicit epochs, exact expiry, equal-minute next-day handling,
midnight wrap, expiry after clock recovery, storage failure, saved-state reload,
clear/reload, tap events and cooldown boundaries, queue failure, stale screen taps,
and empty reply lists. The registration test compiles the production registration
builder with the option off and on: **2333 bytes off, 3868 bytes on**, below 8192.

Baseline issues to distinguish from Door Sign:

- `test_simulator_avatar_selection`: incremental default/custom renderer selection fails before edits.
- Three `test_coexist_bench` cases: the sandbox denies binding the local test server.
- `test_muse_eink_navigation` and two `test_muse_eink_presentation` setup classes: pinned FreeInk source is unavailable.
- `test_muse_ptt_ui`: Apple Clang treats the unused `-no-pie` option as an error.
- `test_muse_voice_call`: the host SDK marks cJSON's `sprintf` usage deprecated under `-Werror`.

The seven skips are real-crypto pairing (IDF/mbedTLS environment), mobile pairing
vectors (`cryptography`), VP8 encoder (`libvpx`), x264 encoder (`libx264`), imported
Noise PSA backend (PSA headers/libraries), power overlay symbol checks (`IDF_PATH`
unset in the host-test shell), and the synthetic power benchmark (pinned
`ppk2-api==0.9.2`).

## Simulator

From `esp32/`:

```sh
cmake --build simulator/build
ctest --test-dir simulator/build --output-on-failure
```

Before edits: **12/12 passed**. Final: **13/13 passed**.
The new CTest runs each geometry twice: **172×320**, **466×466 round**, **320×240**.
It exercises real pointer taps and a left swipe to settings, confirmation timeout,
listening and text-reply visibility, sleep/wake returning home, clear returning
to the face, custom contrast and long text, and zero replies. Every capture is
deterministic. Meeting, sent and custom sign pixels match the checked-in PNGs;
face captures are checked for determinism and distinctness from the sign. The
portrait, round and wide screenshots were also visually inspected.

Update reference captures after reviewing an intentional UI change, from root:

```sh
python3 esp32/simulator/tests/test_door_sign.py --binary esp32/simulator/build/muse_door_sign_test --update
```

## Firmware build boundary

A fresh XIAO build was attempted with ESP-IDF v6.0.1, from `esp32/`:

```sh
. /Users/mengdu/esp/esp-idf/export.sh
IDF_CCACHE_ENABLE=0 idf.py -B build-door-sign -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-door-sign/sdkconfig \
  '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-seeed-xiao-s3-147' build
```

Configuration stopped inside `idf_component_manager` while `psutil` enumerated
processes: `PermissionError: [Errno 1] Operation not permitted (originated from
sysctl() malloc 1/3)`. Firmware compilation was therefore **unavailable in this
sandbox**. App size, overlay application in generated configuration and a second
board's firmware build remain unverified. Serial ports and flashing were outside
this run's scope. Shared gadget event/clock internals and partition tables retain
their existing implementation.

## Hardware review

Build the final reviewed tree with `tools/muse/board.sh build xiao147`, record the
app size and confirm all overlay options in the generated configuration. Also
build another LVGL touch profile with Door Sign disabled and enabled. On the XIAO:

1. Pair with USR1, enter Wi-Fi through touch settings, reconnect and verify `sign.set/get/clear` through Muse.
2. Tap each reply, confirm its exact owner/time in the real Muse chat, and check the ten-second cooldown.
3. Hold USR1, send a visitor voice note, read the reply, and confirm return to the sign.
4. Enter settings by swipe; sleep and wake with USR2. Confirm the sign returns.
5. Set deadlines across midnight and near the current time. Confirm one expiry notice and free-state defaults.
6. Power-cycle active and cleared signs. Verify labels, owner and deadline restore, including an overdue deadline after clock sync.
7. Disconnect Wi-Fi, restore it, check event recovery and the foundation's dropped count.

Record the final revision, board revision, unit count, app size and actual results
when those checks are performed.
