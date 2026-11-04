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

Validated on 2026-11-04 in branch `project/door-sign`, based on `e62777b`, with the
compact layout. Hardware target: Seeed XIAO ESP32-S3 1.47-inch Touch Display,
172×320 JD9853A. Hardware revision is unspecified.
The compact layout was built, flashed and checked on **one physical unit**: text is
sharp and every label is shown in full.

## Readability layout

- Owner: just the name, Montserrat **16 px**, dim white.
- State icon: Montserrat **28 px**, followed by the built-in word **Meeting**,
  **Focus**, **Free**, **Away** or **Custom** at **28 px**. A supplied title uses
  28 px when it fits one line, otherwise **20 px** over up to two lines. Titles
  fitting those two lines appear in full; longer content ends with an ellipsis.
- Deadline: Montserrat **20 px**, one line such as `until 15:00`.
- Reply labels: common messages pair an icon with compact words at **20 px**,
  such as home + **Dinner**, bell + **Need you**, envelope + **Parcel**, phone +
  **Call me**, and checkmark + **OK**. Wider words use **16 px**; labels can use
  two lines, with an ellipsis for content exceeding the available area.
- Three reply buttons: **156×44 px** on the XIAO, separated by 4 px. Their last
  row ends at y=291; the page dots occupy y=298–305.
- The **156×122 px** status card occupies x=8–163, y=26–147. The sign consists
  of the owner, status card and replies. Subtitle remains accepted in the payload;
  narrow screens show the compact layout. An accepted tap shows **checkmark +
  Sent inside that button for three seconds**, then restores its message.
- Reply text is pure white. Card text retains the existing
  luminance-based black/white selection, including black text on a white card.

`lv_text_get_size` measures the incoming text when it changes. The local deadline
uses `until HH:MM`, or a right arrow and `HH:MM` when space is tighter. All three
font sizes are already enabled in `devices/sdkconfig.muse`; every board overlay
retains its existing configuration. The round central-square and wide two-column
layouts show the same compact content. The original payload limits, stored state
defaults and reply-event text remain unchanged. Registration and the Muse skill
recommend one-word titles of at most 10 characters and replies of at most 12.

## Implementation map

| Files | Responsibility |
|---|---|
| `esp32/main/door_sign_model.c`, `.h` | State defaults, bounded parameter parser, absolute deadlines, versioned persistence encoding and tap cooldown |
| `esp32/main/door_sign.c`, `.h` | Background NVS writes, command results, snapshots, reply and expiry events |
| `esp32/components/muse/muse_door_sign.c`, `.h`, `muse_door_sign_view.h` | LVGL sign, reply buttons, deadline, confirmation and shared state contract |
| `esp32/components/muse/muse_ui.c` | Home-tile integration, conversation visibility and wake-to-sign behaviour |
| `esp32/main/app.c`, `noise_control.cpp` | Startup, dispatch and command advertisement behind the same option |
| `esp32/main/CMakeLists.txt`, `esp32/components/muse/CMakeLists.txt`, `esp32/components/muse/Kconfig`, `esp32/devices/sdkconfig.muse-seeed-xiao-s3-147` | Conditional sources and XIAO-only default enablement |
| `esp32/tests/test_door_sign.py`, `door_sign_harness.c` | Production model/worker harness, option gating and full registration-size checks |
| `esp32/simulator/CMakeLists.txt`, `include/sim_board.h`, `src/sim_board.c`, `tests/door_sign_harness.c`, `tests/test_door_sign.py` | Full production UI tests with configurable screen geometry |
| `esp32/simulator/tests/screenshots/door-sign/*.png` | Fifteen reference captures for three geometries |
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
merges unchanged content and refreshes the deadline once per second. Existing
LVGL fonts and icons supply the artwork.

## Tools

- macOS 26.6.2, Apple Clang 21.0.0.
- Python 3.9.6, CMake 4.3.3, Ninja 1.13.2; simulator uses the existing LVGL 9.5.0 checkout.
- ESP-IDF checkout `/Users/mengdu/esp/esp-idf`: `v6.0.1`; Xtensa GCC 15.2.0 (`esp-15.2.0_20251204`).
- Host model/worker tests compile with AddressSanitizer and UndefinedBehaviorSanitizer.
- Linux validation and the remaining physical-device checks below await review.

## Host tests

From `esp32/`:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
python3 -m unittest tests.test_door_sign tests.test_gadget_events
```

The focused acceptance command passes: **9 tests**, including the model/worker,
registration and gadget-event cases, with address and undefined-behaviour checks.

Earlier larger-font revision full suite: **772 tests; 8 errors, 5 skipped**. Exact summary:
`Ran 772 tests in 149.119s`, `FAILED (errors=8, skipped=5)`.
The error categories match the original feature's validation record. The earlier
`test_simulator_avatar_selection` failure passes in the current tree.

The three Door Sign tests pass. The compiled harness covers all state defaults,
invalid fields and types, UTF-8 and lengths, custom colour, duplicate replies,
clock validity, explicit epochs, exact expiry, equal-minute next-day handling,
midnight wrap, expiry after clock recovery, storage failure, saved-state reload,
clear/reload, tap events and cooldown boundaries, queue failure, stale screen taps,
and empty reply lists. The registration test compiles the production registration
builder with the option off and on: **2333 bytes off, 4043 bytes on**, below 8192.
The registration check also verifies the short-title/reply guidance and optional,
hidden-on-narrow-screen subtitle description. Legacy longer payloads remain
covered by the model/worker harness.

Full-suite environment errors:

- Three `test_coexist_bench` cases: the sandbox denies binding the local test server.
- `test_muse_eink_navigation` and two `test_muse_eink_presentation` setup classes: pinned FreeInk source is unavailable.
- `test_muse_ptt_ui`: Apple Clang treats the unused `-no-pie` option as an error.
- `test_muse_voice_call`: the host SDK marks cJSON's `sprintf` usage deprecated under `-Werror`.

The five skips are mobile pairing vectors (`cryptography`), VP8 encoder
(`libvpx`), x264 encoder (`libx264`), imported Noise PSA backend (PSA
headers/libraries), and the synthetic power benchmark (pinned
`ppk2-api==0.9.2`). A focused prerequisite check reproduced all five skips;
real-crypto pairing and power overlay symbol checks pass in this environment.

## Simulator

From `esp32/`:

```sh
cmake --build simulator/build
ctest --test-dir simulator/build --output-on-failure
```

Compact-layout revision: **13/13 passed**.
The Door Sign CTest runs each geometry twice: **172×320**, **466×466 round**, **320×240**.
It exercises real pointer taps and a left swipe to settings, confirmation timeout,
listening and text-reply visibility, sleep/wake returning home, clear returning
to the face, custom contrast and long text, and zero replies. Every capture is
deterministic. Meeting, sent and custom sign pixels match the checked-in PNGs;
face captures are checked for determinism and distinctness from the sign. The
portrait, round and wide screenshots were regenerated and all fifteen PNGs were
visually inspected. Layout assertions check font sizes, white foreground text,
non-overlapping icon/title/deadline, 44 px button heights, all three reply taps,
in-button confirmation, full default reply words and the visible page-dot row.
The custom capture displays `Please come back later` in full at 20 px; the free
capture shows the green `Free` card. Tests also cover a short 28 px custom title,
overflow after the 20/16 px fallbacks, growth back to 20 px after a short reply
replaces a long one, compact reply aliases, original tap payloads, clock
synchronization, the arrow deadline fallback and an absent deadline.

Update reference captures after reviewing an intentional UI change, from root:

```sh
python3 esp32/simulator/tests/test_door_sign.py --binary esp32/simulator/build/muse_door_sign_test --update
```

## Firmware build boundary

`tools/muse/board.sh build xiao147` with the compact layout: app `0x2e1000` bytes,
**28 % app-partition space free**. It uses the Montserrat 16/20/28 fonts already
enabled for Muse boards. A second board's firmware build remains pending.

## Hardware results and remaining review

Results on **one XIAO 1.47 unit** (Muse skill installed by sending `SKILL.md` to the
owner's Muse chat):

| Feature | Physical result |
|---|---|
| Muse sets the sign | Verified |
| Reply tap reaches Muse; Muse replies to the owner | Verified |
| `until` expiry switches the sign back to free | Verified |
| Sharp text | Verified |
| Compact layout shows every label in full | Verified |
| Visitor voice note through USR1 | Verified |
| USR2 sleep/wake | Verified |
| Sign persistence after reboot | Pending |

Build the final reviewed tree with `tools/muse/board.sh build xiao147`, record the
app size, confirm at least 20% free and all overlay options in the generated
configuration. Also build another LVGL touch profile with Door Sign disabled and
enabled. On the XIAO:

1. Pair with USR1, enter Wi-Fi through touch settings, reconnect and verify `sign.set/get/clear` through Muse.
2. Tap each reply, confirm its exact owner/time in the real Muse chat, and check the ten-second cooldown.
3. Hold USR1, send a visitor voice note, read the reply, and confirm return to the sign.
4. Enter settings by swipe; sleep and wake with USR2. Confirm the sign returns.
5. Set deadlines across midnight and near the current time. Confirm one expiry notice and free-state defaults.
6. Power-cycle active and cleared signs. Verify labels, owner and deadline restore, including an overdue deadline after clock sync.
7. Disconnect Wi-Fi, restore it, check event recovery and the foundation's dropped count.

Record the final revision, board revision, unit count, app size and actual results
when those checks are performed.
