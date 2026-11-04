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

"""Production list validation, NVS, VM writes, and feature registration."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

try:
    from .card_test_support import ROOT, JSON, compile_host, run_host, write_rtos, ENV, SANITIZERS
except ImportError:
    from card_test_support import ROOT, JSON, compile_host, run_host, write_rtos, ENV, SANITIZERS


class StickyNotes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.out = out = Path(cls.temp.name)
        write_rtos(out)
        (out / "freertos/queue.h").write_text('''#pragma once
#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned, unsigned);
BaseType_t xQueueSend(QueueHandle_t, const void *, int);
BaseType_t xQueueReceive(QueueHandle_t, void *, int);
''')
        (out / "nvs.h").write_text((ROOT / "tests/link_fakes/nvs.h").read_text() + '''
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
''')
        cls.binaries = {}
        for kind in ("model", "service"):
            binary = compile_host(out, [ROOT / f"tests/sticky_notes_{kind}_harness.c",
                                       ROOT / "main/sticky_notes_model.c"], json=True)
            dest = out / kind
            binary.rename(dest)
            cls.binaries[kind] = dest

    def case(self, kind, name):
        result = subprocess.run([str(self.binaries[kind]), name], text=True, capture_output=True, env=ENV)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_model_commands_and_invalid_parameters_are_atomic(self):
        self.case("model", "validation")

    def test_allocation_failure_keeps_state_and_write_offset_atomic(self):
        self.case("model", "allocation")

    def test_list_item_and_text_limits(self):
        self.case("model", "limits")

    def test_mirror_json_roundtrip_and_utf8_chunk_boundaries(self):
        self.case("model", "mirror")

    def test_nvs_roundtrip_storage_failure_and_busy_queue(self):
        self.case("service", "persistence")

    def test_touch_event_debounce_and_coalescing(self):
        self.case("service", "events")

    def test_mirror_debounce_retry_timeout_and_late_callback(self):
        self.case("service", "mirror")

    def test_option_and_both_command_entry_points(self):
        for filename, marker in (("noise_control.cpp", "sticky_notes_register(commands)"),
                                 ("app.c", "if (sticky_notes_handles(command))")):
            source = (ROOT / "main" / filename).read_text()
            at = source.index(marker)
            guard = source.rindex("#if ", 0, at)
            self.assertEqual(source[guard:source.index("\n", guard)], "#if CONFIG_MUSE_STICKY_NOTES")
            self.assertNotIn("#endif", source[guard:at])
        config = (ROOT / "main/Kconfig.projbuild").read_text().split("config MUSE_STICKY_NOTES\n")[1].split("config ")[0]
        self.assertIn("depends on MUSE_BOARD_RETERMINAL_STICKY && HOMEHUB_DAEMON_REQUESTS", config)
        self.assertIn("select HOMEHUB_GADGET_EVENTS", config)
        self.assertIn("select HOMEHUB_GADGET_CLOCK", config)
        self.assertIn("default n", config)
        enabled = [p.name for p in (ROOT / "devices").glob("sdkconfig.*") if "CONFIG_MUSE_STICKY_NOTES=y" in p.read_text()]
        self.assertEqual(enabled, ["sdkconfig.reterminal-sticky"])

    def test_actual_sticky_registration_and_dispatch_with_option_on_and_off(self):
        source = (ROOT / "main/noise_control.cpp").read_text()
        builder = source[source.index("static cJSON *string_param("):source.index("// ---- OTA callback")]
        app = (ROOT / "main/app.c").read_text()
        start = app.index("#if CONFIG_MUSE_STICKY_NOTES\n    if (sticky_notes_handles(command))")
        dispatch = app[start:app.index("#endif", start) + len("#endif")]
        for enabled in (0, 1):
            with self.subTest(enabled=enabled), tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp)
                cc = shlex.split(os.environ.get("CC", "cc"))
                objects = []
                for name, path in (("json", JSON / "cJSON.c"), ("model", ROOT / "main/sticky_notes_model.c")):
                    obj = out / f"{name}.o"
                    subprocess.run([*cc, "-std=c11", *SANITIZERS, "-Wno-deprecated-declarations", "-I", str(JSON),
                                    "-I", str(ROOT / "main"), "-c", str(path), "-o", str(obj)], check=True, capture_output=True)
                    objects.append(obj)
                harness = out / "registration.cpp"
                harness.write_text('''#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "sticky_notes.h"
}
static char s_wifi_ssid[33]="home", s_register_req_id[40];
static const char *s_node_id="node", *s_display_name="Fridge Notes";
static void copy_wifi_ssid(char *out,size_t n) {snprintf(out,n,"%s",s_wifi_ssid);}
static void make_uuid(char *out,size_t n) {snprintf(out,n,"00000000-0000-0000-0000-000000000000");}
struct esp_app_desc_t {const char *version;};
static const esp_app_desc_t *esp_app_get_description() {static esp_app_desc_t d={"999.0.0"};return &d;}
static bool led_status_display_info(int *w,int *h) {*w=480;*h=800;return true;}
static int led_status_display_bits() {return 2;}
static bool ota_is_enabled() {return true;}
extern "C" cJSON *sticky_notes_command(const char *,const cJSON *,const char *,uint64_t) {return cJSON_CreateObject();}
''' + builder + '''
static cJSON *dispatch(const char *command,cJSON *params,const char *request_id,uint64_t session_generation) {
(void)command;(void)params;(void)request_id;(void)session_generation;
''' + dispatch + '''
return nullptr;
}
int main() {
char *json=build_register_json();assert(json && strlen(json)<8192);
cJSON *root=cJSON_Parse(json);
cJSON *commands=cJSON_GetObjectItem(cJSON_GetObjectItem(root,"params"),"commands_v2");
const char *names[]={"notes.set","notes.add","notes.get","notes.toggle","notes.remove","notes.clear_done"};
for (const char *name:names) {
assert((cJSON_GetObjectItem(commands,name)!=nullptr)==CONFIG_MUSE_STICKY_NOTES);
cJSON *result=dispatch(name,nullptr,"request",1);assert((result!=nullptr)==CONFIG_MUSE_STICKY_NOTES);cJSON_Delete(result);
}
printf("Sticky registration notes=%d: %zu bytes\\n",CONFIG_MUSE_STICKY_NOTES,strlen(json));
cJSON_Delete(root);free(json);
}
''')
                flags = [f"-DCONFIG_MUSE_STICKY_NOTES={enabled}", "-DCONFIG_HOMEHUB_GADGET_EVENTS=1",
                         "-DCONFIG_HOMEHUB_GADGET_CLOCK=1", "-DCONFIG_HOMEHUB_DISPLAY_COMMANDS=1",
                         "-DCONFIG_HOMEHUB_RETERMINAL_SHT4X=1", "-DCONFIG_HOMEHUB_TUNNEL=1",
                         "-DCONFIG_HOMEHUB_LED_BACKEND_MUSE=1"]
                binary = compile_host(out, [harness, *objects], flags=flags, cxx=True)
                result = run_host(binary)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                print(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
