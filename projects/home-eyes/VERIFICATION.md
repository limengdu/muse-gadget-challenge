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
