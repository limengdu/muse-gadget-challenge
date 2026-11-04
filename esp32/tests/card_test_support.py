# Copyright (c) 2026 limengdu
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Compile host harnesses with the production cJSON and local RTOS fakes."""
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
JSON = Path(os.environ.get("CJSON_SOURCE_DIR", ROOT / "managed_components/espressif__cjson/cJSON"))
SANITIZERS = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
ENV = {**os.environ, "ASAN_OPTIONS": "detect_leaks=0:abort_on_error=1",
       "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}


def compile_host(out, sources, *, json=False, flags=(), cxx=False):
    binary = out / "harness"
    command = shlex.split(os.environ.get("CXX" if cxx else "CC", "c++" if cxx else "cc"))
    command += ["-std=c++17" if cxx else "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                *SANITIZERS, *flags, "-I", str(out), "-I", str(JSON),
                "-I", str(ROOT / "main"), "-I", str(ROOT / "tests/link_fakes")]
    if json and not cxx:
        obj = out / "cjson.o"
        compiled = subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11",
                                   *SANITIZERS, "-DCJSON_NESTING_LIMIT=16", "-Wno-deprecated-declarations", "-c",
                                   str(JSON / "cJSON.c"), "-o", str(obj)],
                                  text=True, capture_output=True, env=ENV)
        if compiled.returncode:
            raise AssertionError(compiled.stdout + compiled.stderr)
        sources = [*sources, obj]
    command += [*map(str, sources), "-lm", "-o", str(binary)]
    result = subprocess.run(command, text=True, capture_output=True, env=ENV)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return binary


def run_host(binary):
    return subprocess.run([str(binary)], text=True, capture_output=True, env=ENV)


def write_rtos(out):
    headers = {
        "freertos/task.h": """#pragma once
#include "freertos/FreeRTOS.h"
#include <stdint.h>
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreate(TaskFunction_t, const char *, unsigned, void *, unsigned, TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
void vTaskDelay(int);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
void xTaskNotifyGive(TaskHandle_t);
""",
        "esp_timer.h": """#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
""",
    }
    for name, text in headers.items():
        p = out / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
