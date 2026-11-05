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

# Home Eyes validation

## RAM margin follow-up (2026-11-05)

Revision: `a6367d3` plus this uncommitted working tree on `project/home-eyes`.
Target: SenseCAP Watcher, ESP32-S3, 8 MiB PSRAM. Toolchain: ESP-IDF v6.0.1,
Xtensa GCC 15.2.0 (`esp-15.2.0_20251204`). The hardware observations below
were supplied for one Watcher running the baseline; its board revision is
unknown. This follow-up used host tests and target compilation, with zero
device accesses.

### Baseline hardware finding

Pairing worked. `camera.capture` returned a scene Muse described. `home.say`
showed a message card; wheel acknowledgement produced a `home-eyes` event and
Muse informed the owner. `watch.start` worked. Later, `watch.status` reached
`invoke request: command=watch.status`, but no result followed and Muse reported
`node_invoke_timeout`. The tunnel logged `keepalive timeout (65s idle)` and
then `retrying tunnel stream open` / `server reset tunnel stream` every five
seconds. Typed turns were interrupted.

Heartbeat `dma=free/largest` was 24/11 KiB at unpaired boot, 16/11 KiB connected
and idle at t=162 s before a watch, then 15/11 KiB after camera checks, remaining
there. `noise_tx_has_dma_headroom()` gates both queued invoke results and tunnel
pings at `NOISE_TX_DMA_RESERVE_BYTES = 16 * 1024`. These observations and the
send-path gate explain why accepted commands and keepalives could remain queued.
The connected-device acceptance floor is 22 KiB free DMA, 6 KiB above that gate.

### Static RAM measurement

Baseline values come from `build-muse-sensecap-watcher/muse-gadget.map`, checked
with the installed `esp_idf_size --files` reader and the existing target objects.
After values come from recompiling these translation units with the exact
existing `compile_commands.json` options, plus `-fstack-usage` and warnings as
errors. Matching dependency versions were read from the local component cache.
They are object-section payloads in bytes; final-link alignment and heap metadata
are separate. Full firmware linking was blocked as described below.

| Object | Internal .bss before → after | Internal .data before → after | PSRAM .bss before → after |
|---|---:|---:|---:|
| home_eyes.c.obj | 101 → 22 | 0 → 0 | 6,912 → 8,016 |
| home_eyes_core.c.obj | 0 → 0 | 0 → 0 | 0 → 0 |
| muse_home_eyes_ui.c.obj | 878 → 38 | 8 → 8 | 0 → 840 |
| watcher_camera.c.obj | 14 → 14 | 0 → 0 | 0 → 0 |
| gadget_events.c.obj | 847 → 246 | 0 → 0 | 0 → 601 |
| camera_sscma.c.obj | 105 → 105 | 20 → 20 | 0 → 0 |
| Total | 1,945 → 425 | 28 → 28 | 6,912 → 9,457 |

The four requested Home Eyes/UI/camera objects use 1,001 → 82 bytes of internal
static data. Including event storage and the camera driver gives 1,973 → 453
bytes: **1,520 bytes released from internal static RAM**.

`s_state` and `s_snapshot` already occupied 3,456 bytes each in PSRAM, including
two 1,024-byte baseline/candidate frames per state. The Watcher overlay and the
generated configuration both already enable
`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`. This fix retains that placement
and moves the 1,024-byte worker thumbnail, 80-byte error text, 656-byte UI view,
112-byte badge text, two 36-byte font descriptors and 601-byte event reply into
external BSS. Locks, atomics and task handles remain internal.

### Runtime allocations and stack budget

| Allocation | Baseline | After |
|---|---|---|
| home_eyes worker stack | 6,144 bytes internal, including its 1,024-byte thumbnail | 4,096 bytes internal; thumbnail in external BSS |
| gadget_events sender stack | 6,144 bytes internal | 6,144 bytes PSRAM, with its task control block internal |
| Restore scratch state | 3,456 bytes through ordinary calloc | 3,456 bytes explicitly allocated in PSRAM, freed after validation |
| Retained event records and strings | Ordinary calloc, normally internal below the configured 4,096-byte threshold | Explicit PSRAM allocation for Home Eyes |
| Camera SPI staging while powered | 256-byte TX + 4,096-byte RX, internal DMA | 256-byte TX + 512-byte RX, internal DMA |
| Thumbnail decoder | 6,168-byte accumulator + 3,100-byte ROM JPEG pool in PSRAM | Same PSRAM allocations, freed after each check |

The worker still calls `config_set_str()` / NVS synchronously and uses an
internal stack. The event sender only submits queued requests and processes
responses, so its stack can use PSRAM. Event memory changes are gated by
`CONFIG_MUSE_HOME_EYES && CONFIG_SPIRAM`; event ordering, retries, request formats
and delivery behavior retain their existing implementation. The Link reserve
and shared send path are unchanged.

GCC's exact-option stack report measures the worker's own frame at 1,184 bytes
before and 96 bytes after. After outlining, `publish` uses 784 bytes and
`post_event` 544 bytes; the largest known worker/helper frame sum is 880 bytes.
These figures exclude nested library/IDF/ROM calls and interrupts. The 4,096-byte
allocation retains 3,216 bytes beyond that known sum; runtime high-water
measurement is still required. After the first successful capture followed by
a successful state save, firmware emits one
`link.stack: home_eyes stack high water mark: ... bytes free` line. The host
harness checks its timing and single emission; its fake stack value is a test
input, separate from hardware evidence.

Home Eyes creates five LVGL labels and two base objects: target type sizes give
692 bytes of object headers, plus content, styles and layout allocations. These
already use the existing PSRAM-first LVGL allocator. Cards add no canvas. The
existing 412×412 RGB565 camera canvas, when preview is used, is 339,488 bytes in
PSRAM. JPEG payloads, preview frame copies, base64 results and SSCMA reply
assembly already use PSRAM. The JPEG decoder's 124-byte descriptor remains on
the calling task's stack. Capture releases its JPEG; thumbnail decoding frees
both scratch allocations on success and failure. Preview retains only its latest
frame until replacement or preview stop.

Camera reads now request at most 512 bytes with a word-aligned staging capacity;
larger replies are assembled by the existing parser. The DMA stage saves 3,584
bytes during capture. A final short SPI read can additionally need a temporary
driver buffer of up to 512 bytes. More read transactions can increase capture
latency; real preview cadence and capture timing remain device checks.

### Expected DMA margin and acceptance

The fixed internal saving is **8,192 bytes of reserved task stacks + 1,520 bytes
of static data = 9,712 bytes (9.48 KiB)**. Moving retained event records adds a
variable saving that is excluded from this estimate. The thumbnail is accounted
for within the old stack allocation, so its move contributes through the stack
reduction rather than a second independent saving.

Applied to the reported 15 KiB after checks, the estimate is **24.48 KiB free
between checks**; the 16 KiB connected idle observation predicts 25.48 KiB.
Subtracting 768 bytes of camera staging predicts 23.73 KiB during capture, or
about 23.23 KiB with a 512-byte short-read driver buffer. Heap metadata, final-link
padding, NVS/JSON/network transients and runtime variation still require device
measurement. These are budget estimates, not measured new DMA minima or a
guarantee of the 22 KiB floor. The largest free block also requires remeasurement.

On the fixed build, run a connected watch at `interval_min=2` for at least one
hour. Record DMA free/largest through captures, successful `watch.status` replies,
tunnel keepalives and uninterrupted typed turns, plus the one-time stack line.
Exercise scene change → event → `camera.capture` → `watch.verdict`, message and
alert acknowledgements, save/reboot recovery and preview alongside checks.
Acceptance requires DMA free ≥ 22 KiB throughout, with bounded stack usage and
capture latency. This device acceptance remains open.

### Device result on the fixed build (one Watcher)

`tools/muse/board.sh build watcher`: app `0x231000` bytes, 45 % of the 4 MiB slot
free. Flashed and connected, with a watch at `interval_min=2`, low sensitivity:

| Check | Result |
|---|---|
| Heartbeat DMA free/largest, connected, across two worker captures (5 minutes) | **23K/17K** throughout (previously 16K idle, 15K after checks) |
| Internal free/largest | 26K/17K (previously 20K/11K) |
| Tunnel keepalive | `raw=up` throughout; no keepalive timeout or stream reset |
| `watch.status` through Muse | Answered: name, interval, until, next check |
| Typed turns | Completed without interruption |
| Camera capture | 640×480 JPEG in 0.90 s (0.82 s with the 4 KB read budget) |

The one-time stack line did not appear in this window; the worker ran two
captures and saves on its 4 KiB stack without a fault. The one-hour run, the
stack line, scene change → verdict and reboot recovery remain open.

### Checks in this follow-up

From `esp32/`, with `CJSON_SOURCE_DIR` pointing to the matching cached
`espressif/cjson` 1.7.19~2 `cJSON/` directory:

~~~sh
python3 -m unittest tests.test_home_eyes tests.test_gadget_events tests.test_camera
~~~

Result: 14 tests passed, zero skips. Coverage includes production worker state
and save/capture flow, the one-time stack log, explicit PSRAM restore/event
allocations, event delivery with Home Eyes on and off, repeated thumbnail
captures and allocation/decoder failure cleanup, and camera read reconstruction
across packet boundaries with both read budgets. Target compilation of all six
objects in the table passed with warnings treated as errors. Reports and target
objects are in `/tmp/home-eyes-ram/{before-exact,after-exact}/`.

The firmware build used the helper's equivalent Watcher `idf.py` arguments with
the existing private build configuration. CMake's component manager stopped on
the sandbox-denied `psutil` process query (`PermissionError`, `sysctl`), before
the updated firmware could link. The build log is `/tmp/home-eyes-idf-build.log`.
The existing baseline app is **2,297,856 bytes, 45.21% free in a 4 MiB slot**;
the user's hardware build reported 45% free. The fixed app's linked size is
unverified. Run `tools/muse/board.sh build watcher` in the normal IDF environment
to complete that check. Serial, flashing and device interaction were outside
this follow-up's checks; changes remain uncommitted.

## Earlier validation (2026-11-04)

The remaining sections record the initial implementation's validation.

Validation date: 2026-11-04. Revision: base b227353 plus the uncommitted Home Eyes
working tree on project/home-eyes. Hardware units exercised: 0; board revision
unavailable. Board target: SenseCAP Watcher, ESP32-S3, existing board profile.

## Changed files

| Area | Files |
|---|---|
| Engine and persistence | esp32/main/home_eyes.c, home_eyes.h, home_eyes_core.c, home_eyes_core.h |
| Link registration, dispatch and startup | esp32/main/noise_control.cpp, app.c, CMakeLists.txt |
| Round-screen cards and input | esp32/components/muse/muse_home_eyes_ui.c, muse_home_eyes_ui.h, muse_ui.c, muse_input.c, muse_app.c |
| Camera thumbnail adapter | esp32/components/muse/boards/watcher_camera.c, watcher_camera.h |
| Feature configuration | esp32/components/muse/Kconfig, CMakeLists.txt; esp32/devices/sdkconfig.muse-sensecap-watcher |
| Host coverage | esp32/tests/test_home_eyes.py; home_eyes_harness.c, home_eyes_worker_harness.c, home_eyes_camera_harness.c; test_simulator_avatar_selection.py fixture |
| Simulator | esp32/simulator/CMakeLists.txt, compat/sdkconfig.h, lv_conf.h, src/main.c, tests/test_simulator.py; tests/scenarios/home-eyes-{watching,message,verdict,alert}.txt |
| Documentation | esp32/devices/README.md; esp32/simulator/README.md; projects/home-eyes/README.md, README.zh-CN.md, SKILL.md, VERIFICATION.md |

## Design

The feature uses a deadline-driven worker with an internal stack for NVS writes.
Large state snapshots and thumbnail decoding buffers use PSRAM. Link commands
validate parameters and update a bounded state model immediately; the worker
persists accepted changes. Pictures and reference thumbnails remain in RAM.

The existing camera owns power and capture serialization. ROM JPEG decoding
at 1/8 scale feeds 32×32 grayscale averages. A 3×3 blur and exposure-offset
correction precede mean absolute difference thresholds. A pending change
waits for Muse's verdict before reference replacement. Alerts pause further
checks until wheel acknowledgement.

Cards use the existing LVGL face timer. A wheel gesture acknowledges the
visible card and consumes its release; usual talk and double-click behavior
continue outside cards. Chimes are requested through the existing voice task.
An unchanged periodic check preserves the screen's sleeping state.
Wi-Fi remains associated in modem sleep for remote messages while the existing
CPU/display/codec rest path remains available. Battery impact requires measurement.

The foundation event and clock modules are used through public APIs.
Their implementation is unchanged. Foundation delivery remains best effort;
reboot can replay a pending request and offline queue pressure can drop events.

## Host checks

Tools: Python 3.9.6, Apple Clang 21.0.0, CMake 4.3.3.
The new C harnesses use address/undefined-behavior sanitizers.

From esp32/:

~~~sh
python3 -m unittest discover -s tests -p 'test_home_eyes.py'
python3 -m unittest discover -v -s tests -p 'test_*.py'
~~~

Home Eyes: 5 tests passed. The tests execute production core transitions,
the background worker with fake camera/clock/storage/events, and the production
thumbnail adapter against a fake ROM decoder. The real ROM JPEG bitstream
decoder remains a firmware/hardware check.

The full production Watcher registration measures 4,891 bytes with Home Eyes
enabled, and 3,507 bytes with Home Eyes disabled, including camera, clock,
events, display, tunnel and OTA commands. Both fit the 8 KiB registration limit.
Source guards cover advertisement, dispatch, UI and input under the same option.

Full suite: Ran 762 tests in 135.971s.
Result: FAILED (failures=1, errors=8, skipped=7).

The remaining failure is the existing incremental custom-avatar selection
test on this macOS environment. Three errors come from sandbox-denied local
HTTP socket binding in test_coexist_bench. Three FreeInk setup errors require
the pinned source checkout. The two other setup errors are the existing
macOS compiler failures: -no-pie treated as unused in test_muse_ptt_ui, and
cJSON sprintf deprecation treated as an error in test_muse_voice_call.
The initial full run reproduced these same categories before the simulator
fixture was extended for the new overlay source.

The seven skips are:

- Real-crypto pairing: IDF_PATH/MBEDTLS_SOURCE_DIR absent in the host-test shell.
- Pairing transcript vectors: Python cryptography unavailable.
- VP8 steady loops: libvpx encoder development package unavailable.
- x264 steady loops: libx264 development package unavailable.
- Noise core: PSA Crypto headers/libraries unavailable.
- Power-test Kconfig-symbol audit: IDF_PATH absent in the host-test shell.
- PPK2 synthetic benchmark: pinned ppk2-api 0.9.2 unavailable.

Project relative links and git diff --check pass.

## Firmware build

ESP-IDF checkout: v6.0.1, /Users/mengdu/esp/esp-idf.
A fresh build was attempted using the Watcher helper's equivalent IDF arguments:

~~~sh
idf.py -B build-home-eyes-check -DIDF_TARGET=esp32s3   -DSDKCONFIG=build-home-eyes-check/sdkconfig   -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-sensecap-watcher" build
~~~

CMake stopped before firmware compilation: the component manager's psutil
parent-process query was rejected by the sandbox (PermissionError, sysctl).
Firmware/app sizes and generated overlay application remain unverified.
The reviewer can run tools/muse/board.sh build watcher with ESP-IDF v6.0.1
and check the generated configuration and app-size line.

## Simulator

~~~sh
cmake -S esp32/simulator -B esp32/simulator/build-home-eyes -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build esp32/simulator/build-home-eyes --parallel
ctest --test-dir esp32/simulator/build-home-eyes --output-on-failure
~~~

Configuration reached the pinned LVGL 9.5.0 dependency download. Its configured
local proxy was unreachable in the sandbox, so build and CTest could not run.
The added 412×412 scenarios and dismiss-to-face assertions await a successful
simulator build. Visual layout and touch scrolling remain unverified.

## Device verification

Use a factory-backed-up Watcher and a fresh firmware build to check:

1. Pairing/reconnect, remote camera.capture and a full watch event → fresh photo
   → verdict → reference-update cycle.
2. Wheel talk, double-click preview/shutter, settings navigation, card scrolling,
   card priority, read events and acknowledgement while new cards arrive.
3. Chime volume, five-second alarm repetition/stop and the single ten-minute
   unread-message reminder; message/verdict/watch restoration after reboot.
4. End-time expiry after clock synchronization, camera contention and failures,
   remote access on battery, camera power during captures, CPU/display/codec
   rest between checks, and measured battery runtime.

These checks establish physical behavior separately from host test results.


## ESP32-S3 cross-compiler syntax checks

The restored-state parser uses braced conditional branches inside LOAD and
TEXT, with one macro invocation per line. Cleanup statements are also
separate. Validation ranges, assignments, state transitions and error
handling retain their existing behavior.

Compiler: xtensa-esp32s3-elf-gcc / g++ 15.2.0
(crosstool-NG esp-15.2.0_20251204), ESP-IDF v6.0.1.

~~~sh
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf6.0_py3.14_env
. ~/esp/esp-idf/export.sh
xtensa-esp32s3-elf-gcc -fsyntax-only -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -std=gnu17 ...
xtensa-esp32s3-elf-g++ -fsyntax-only -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -std=gnu++2b ...
~~~

The checks retain include paths, definitions, forced includes and toolchain
response files from build-muse-sensecap-watcher/compile_commands.json.
The response files select the configured Picolibc headers.
Cleared managed-component paths are mapped to the same versions in the local
Espressif ComponentManager cache, including LVGL 9.5.0, esp_lvgl_adapter 0.6.4,
esp_codec_dev 1.5.11, cJSON 1.7.19~2 and esp_websocket_client 1.2.3.

| Translation unit | Result and header context |
|---|---|
| esp32/components/muse/muse_home_eyes_ui.c | PASS; production headers and matching cached dependencies |
| esp32/main/home_eyes.c | PASS; production headers and matching cached dependencies |
| esp32/main/home_eyes_core.c | PASS; production headers and matching cached dependencies |
| esp32/components/muse/boards/watcher_camera.c | PASS; production headers and matching cached dependencies |
| esp32/components/muse/muse_app.c | PASS; production headers and matching cached dependencies |
| esp32/components/muse/muse_input.c | PASS; production headers and matching cached dependencies |
| esp32/components/muse/muse_ui.c | PASS; production headers and matching cached dependencies |
| esp32/main/app.c | PASS; production headers and matching cached dependencies |
| esp32/main/noise_control.cpp | PASS; production headers and matching cached dependencies |
| esp32/tests/home_eyes_worker_harness.c | PASS; existing host-test fake device headers |
| esp32/tests/home_eyes_camera_harness.c | PASS; existing host-test fake device headers |
| esp32/tests/home_eyes_harness.c | PASS; production headers |
| esp32/simulator/src/main.c | PASS; simulator compatibility headers and SDL minimal platform configuration |

All 13 changed/new C/C++ translation units pass with warnings treated as
errors. The nine firmware units use production dependency headers. Host
harnesses reuse their existing fake-device interfaces. The simulator has no
ESP32-S3 SDL platform configuration, so its syntax check selects SDL's
provided SDL_config_minimal.h through a temporary configuration wrapper.
This checks the source with the target compiler; desktop linking and
rendering remain separate checks.

Reproduction argument vectors, dependency mappings and per-file diagnostics
are in /tmp/home-eyes-cross-check/. The driver scripts are
/tmp/home_eyes_cross_check.py and /tmp/home_eyes_cross_extra.py.

Host regression command (from esp32/), with CJSON_SOURCE_DIR set to the
matching cached component:

~~~sh
python3 -m unittest discover -s tests -p 'test_home_eyes.py'
~~~

Result: 5 tests passed, including state restoration and production command
registration. Firmware linking, app size and device behavior remain part of
the device acceptance checks above.
