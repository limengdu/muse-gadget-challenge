#
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


from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

try:
    from .card_test_support import ROOT, compile_host, run_host, write_rtos
except ImportError:
    from card_test_support import ROOT, compile_host, run_host, write_rtos


class DoorSign(unittest.TestCase):
    def test_model_and_worker(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            write_rtos(out)
            (out / "freertos/queue.h").write_text("""#pragma once
#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned, unsigned);
void vQueueDelete(QueueHandle_t);
BaseType_t xQueueSend(QueueHandle_t, const void *, TickType_t);
BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t);
""")
            (out / "esp_log.h").write_text('#define ESP_LOGW(tag, ...) ((void)(tag))\n')
            binary = compile_host(out, [ROOT / "tests/door_sign_harness.c", ROOT / "main/door_sign_model.c"],
                                  json=True, flags=["-I", str(ROOT / "components/muse")])
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_xiao_registration_fits_with_option_on_and_off(self):
        try:
            from .card_test_support import JSON
        except ImportError:
            from card_test_support import JSON
        source = (ROOT / "main/noise_control.cpp").read_text()
        builder = source[source.index("static cJSON *string_param("):source.index("// ---- OTA callback")]
        sign = (ROOT / "main/door_sign.c").read_text()
        schema = sign[sign.index("static void parameter("):]
        for enabled in (0, 1):
            with self.subTest(enabled=enabled), tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp)
                obj = out / "json.o"
                subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-c", str(JSON / "cJSON.c"),
                                "-o", str(obj)], check=True, capture_output=True)
                harness = out / "register.cpp"
                harness.write_text("""#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cJSON.h"
static char s_wifi_ssid[33]="home", s_register_req_id[40];
static const char *s_node_id="node", *s_display_name="Door Sign";
static void copy_wifi_ssid(char *out, size_t n) {snprintf(out,n,"%s",s_wifi_ssid);}
static void make_uuid(char *out, size_t n) {snprintf(out,n,"00000000-0000-0000-0000-000000000000");}
struct esp_app_desc_t {const char *version;};
static const esp_app_desc_t *esp_app_get_description() {static esp_app_desc_t d={"999.0.0"};return &d;}
static bool led_status_display_info(int *w,int *h) {*w=172;*h=320;return true;}
static int led_status_display_bits() {return 16;}
static bool ota_is_enabled() {return false;}
""" + schema + builder + """
int main() {
    char *json=build_register_json(); assert(json && strlen(json)<8192);
    cJSON *root=cJSON_Parse(json);
    cJSON *commands=cJSON_GetObjectItem(cJSON_GetObjectItem(root,"params"),"commands_v2");
    const char *names[]={"sign.set","sign.get","sign.clear"};
    for (const char *name:names) assert((cJSON_GetObjectItem(commands,name)!=nullptr)==CONFIG_MUSE_DOOR_SIGN);
    if (CONFIG_MUSE_DOOR_SIGN) {
        cJSON *optional=cJSON_GetObjectItem(cJSON_GetObjectItem(commands,"sign.set"),"optional");
        const char *title=cJSON_GetObjectItem(cJSON_GetObjectItem(optional,"title"),"description")->valuestring;
        const char *replies=cJSON_GetObjectItem(cJSON_GetObjectItem(optional,"replies"),"description")->valuestring;
        const char *subtitle=cJSON_GetObjectItem(cJSON_GetObjectItem(optional,"subtitle"),"description")->valuestring;
        assert(strstr(title,"<=10 characters") && strstr(title,"state word"));
        assert(strstr(replies,"<=12 characters") && strstr(replies,"Dinner"));
        assert(strstr(subtitle,"Optional") && strstr(subtitle,"not shown on narrow screens"));
    }
    printf("XIAO registration option=%d: %zu bytes\\n",CONFIG_MUSE_DOOR_SIGN,strlen(json));
    free(json); cJSON_Delete(root);
}
""")
                binary = compile_host(out, [harness, obj], cxx=True, flags=[
                    f"-DCONFIG_MUSE_DOOR_SIGN={enabled}", f"-DCONFIG_HOMEHUB_GADGET_EVENTS={enabled}",
                    f"-DCONFIG_HOMEHUB_GADGET_CLOCK={enabled}", "-DCONFIG_HOMEHUB_DISPLAY_COMMANDS=1",
                    "-DCONFIG_HOMEHUB_LED_BACKEND_MUSE=1", "-DCONFIG_HOMEHUB_TUNNEL=1"])
                result = run_host(binary)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                print(result.stdout.strip())

    def test_advertised_and_dispatched_only_under_option(self):
        noise = (ROOT / "main/noise_control.cpp").read_text()
        registration = noise[noise.index("static char *build_register_json"):]
        block = registration.split("#if CONFIG_MUSE_DOOR_SIGN", 1)[1].split("#endif", 1)[0]
        self.assertIn("door_sign_register(commands)", block)
        app = (ROOT / "main/app.c").read_text().split("static cJSON *on_ws_command(", 1)[1]
        block = app.split("#if CONFIG_MUSE_DOOR_SIGN", 1)[1].split("#endif", 1)[0]
        for command in ("sign.set", "sign.get", "sign.clear"):
            self.assertIn('"' + command + '"', block)
        self.assertIn("door_sign_command(", block)
        cmake = (ROOT / "main/CMakeLists.txt").read_text()
        self.assertIn('if(CONFIG_MUSE_DOOR_SIGN)\n    list(APPEND GADGET_SRCS "door_sign.c" "door_sign_model.c")', cmake)
        overlays = list((ROOT / "devices").glob("sdkconfig.*"))
        enabled = [p.name for p in overlays if "CONFIG_MUSE_DOOR_SIGN=y" in p.read_text()]
        self.assertEqual(enabled, ["sdkconfig.muse-seeed-xiao-s3-147"])


if __name__ == "__main__":
    unittest.main()
