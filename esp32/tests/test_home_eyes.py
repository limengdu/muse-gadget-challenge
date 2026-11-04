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
import tempfile
import os
import shlex
import subprocess
import unittest

try:
    from .card_test_support import ROOT, JSON, compile_host, run_host, write_rtos
except ImportError:
    from card_test_support import ROOT, JSON, compile_host, run_host, write_rtos


class HomeEyes(unittest.TestCase):
    def test_core(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = compile_host(Path(tmp), [ROOT / 'tests/home_eyes_harness.c',
                                             ROOT / 'main/home_eyes_core.c'], json=True)
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_watcher_registration_size_and_schema(self):
        source = (ROOT / "main/noise_control.cpp").read_text()
        builder = source[source.index("static cJSON *string_param("):source.index("// ---- OTA callback")]
        for enabled in (0, 1):
            with self.subTest(enabled=enabled), tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp)
                harness = out / "registration.cpp"
                harness.write_text("""
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "cJSON.h"
#include "home_eyes.h"
}
static char s_wifi_ssid[33]="home", s_register_req_id[40];
static const char *s_node_id="node", *s_display_name="SenseCAP Watcher";
static void copy_wifi_ssid(char *out,size_t n) {snprintf(out,n,"%s",s_wifi_ssid);}
static void make_uuid(char *out,size_t n) {snprintf(out,n,"00000000-0000-0000-0000-000000000000");}
struct esp_app_desc_t {const char *version;};
static const esp_app_desc_t *esp_app_get_description() {static esp_app_desc_t d={"999.0.0"};return &d;}
static bool led_status_display_info(int *w,int *h) {*w=412;*h=412;return true;}
static int led_status_display_bits() {return 16;}
static bool ota_is_enabled() {return true;}
""" + builder + """
int main() {
    char *json=build_register_json(); assert(json && strlen(json)<8192);
    cJSON *root=cJSON_Parse(json);
    cJSON *commands=cJSON_GetObjectItem(cJSON_GetObjectItem(root,"params"),"commands_v2");
    const char *names[]={"watch.start","watch.stop","watch.status","watch.verdict","home.say"};
    for (auto name:names) {
        cJSON *cmd=cJSON_GetObjectItem(commands,name);
        assert((cmd!=nullptr)==CONFIG_MUSE_HOME_EYES);
        if (cmd) {assert(cJSON_IsObject(cJSON_GetObjectItem(cmd,"required")));
                  assert(cJSON_IsObject(cJSON_GetObjectItem(cmd,"optional")));}
    }
    printf("Watcher registration: %zu bytes",strlen(json));
    free(json);cJSON_Delete(root);
}
""")
                objects = []
                for name, file in [('json', JSON / 'cJSON.c'), ('core', ROOT / 'main/home_eyes_core.c')]:
                    obj = out / (name + '.o')
                    result = subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11',
                                             '-Wno-deprecated-declarations', '-I', str(JSON),
                                             '-I', str(ROOT / 'main'), '-c', str(file), '-o', str(obj)],
                                            text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    objects.append(obj)
                flags = [f'-DCONFIG_MUSE_HOME_EYES={enabled}', '-DCONFIG_MUSE_WATCHER_CAMERA=1',
                         '-DCONFIG_HOMEHUB_GADGET_EVENTS=1', '-DCONFIG_HOMEHUB_GADGET_CLOCK=1',
                         '-DCONFIG_HOMEHUB_DISPLAY_COMMANDS=1', '-DCONFIG_HOMEHUB_LED_BACKEND_MUSE=1',
                         '-DCONFIG_HOMEHUB_TUNNEL=1']
                binary = compile_host(out, [harness, *objects], flags=flags, cxx=True)
                result = run_host(binary)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                print(result.stdout)

    def test_worker_with_fake_camera_clock_storage_and_events(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            write_rtos(out)
            (out / "sdkconfig.h").write_text("#pragma once\n")
            (out / 'esp_attr.h').write_text('#define EXT_RAM_BSS_ATTR\n')
            (out / 'esp_heap_caps.h').write_text('#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n'
                                               '#define MALLOC_CAP_8BIT 2\n'
                                               '#define heap_caps_malloc(n,c) malloc(n)\n')
            (out / 'esp_log.h').write_text('#define ESP_LOGW(tag, ...) ((void)(tag))\n')
            (out / 'esp_err.h').write_text('typedef int esp_err_t;\n')
            (out / 'freertos/queue.h').write_text('#pragma once\ntypedef void *QueueHandle_t;\n')
            sem = out / 'freertos/semphr.h' 
            sem.write_text('#include "freertos/FreeRTOS.h"\ntypedef void *SemaphoreHandle_t;\n'
                           'SemaphoreHandle_t xSemaphoreCreateMutex(void);\n'
                           'int xSemaphoreTake(SemaphoreHandle_t,int);\n'
                           'int xSemaphoreGive(SemaphoreHandle_t);\nvoid vSemaphoreDelete(SemaphoreHandle_t);\n')
            binary = compile_host(out, [ROOT / 'tests/home_eyes_worker_harness.c',
                                       ROOT / 'main/home_eyes_core.c'], json=True,
                                  flags=['-D_POSIX_C_SOURCE=200809L', '-DCONFIG_MUSE_HOME_EYES=1',
                                         '-I', str(ROOT / 'components/muse')])
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_thumbnail_adapter_and_camera_ownership(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            headers = {
                'sdkconfig.h': '#pragma once\n',
                'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NO_MEM 1\n#define ESP_ERR_TIMEOUT 2\n#define ESP_ERR_NOT_SUPPORTED 3\nconst char *esp_err_to_name(int);\n',
                'esp_heap_caps.h': '#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n#define heap_caps_malloc(n,c) malloc(n)\n#define heap_caps_calloc(n,s,c) calloc(n,s)\n',
                'esp_log.h': '#define ESP_LOGW(tag, ...) ((void)(tag))\n#define ESP_LOGE(tag, ...) ((void)(tag))\n',
                'freertos/FreeRTOS.h': '#pragma once\n#define pdPASS 1\n#define portMAX_DELAY -1\n',
                'freertos/semphr.h': '#pragma once\ntypedef void *SemaphoreHandle_t;\nSemaphoreHandle_t xSemaphoreCreateMutex(void);\nint xSemaphoreTake(SemaphoreHandle_t,int);\nint xSemaphoreGive(SemaphoreHandle_t);\n',
                'freertos/task.h': '#include <stddef.h>\nvoid vTaskDeleteWithCaps(void *);\n',
                'freertos/idf_additions.h': '#include <stddef.h>\nint xTaskCreateWithCaps(void (*)(void *),const char *,unsigned,void *,unsigned,void *,unsigned);\n',
                'mbedtls/base64.h': '#include <stddef.h>\nint mbedtls_base64_encode(unsigned char *,size_t,size_t *,const unsigned char *,size_t);\n',
                'muse_board.h': '#pragma once\ntypedef struct {int (*display_lock)(int);void (*display_unlock)(void);} board_t;\nextern const board_t *muse_board;\n',
                'muse_state.h': 'void muse_state_set_caption(const char *,...);\n',
                'muse_ui.h': '#include <stdbool.h>\n#include <stdint.h>\nvoid muse_ui_image_draw(int,int,int,int,const uint16_t *);\nvoid muse_ui_image_hide(void);\nvoid muse_ui_camera_hint(bool);\nvoid muse_ui_show_face(void);\n',
                'rom/tjpgd.h': '#pragma once\ntypedef unsigned UINT;typedef unsigned char BYTE;\ntypedef struct {unsigned left,right,top,bottom;} JRECT;\ntypedef struct {void *device;unsigned width,height;} JDEC;\n#define JDR_OK 0\nint jd_prepare(JDEC *,UINT(*)(JDEC *,BYTE *,UINT),void *,UINT,void *);\nint jd_decomp(JDEC *,UINT(*)(JDEC *,void *,JRECT *),BYTE);\n',
            }
            for name, content in headers.items():
                path = out / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            binary = compile_host(out, [ROOT / 'tests/home_eyes_camera_harness.c'],
                                  flags=['-DCONFIG_MUSE_HOME_EYES=1', '-I', str(ROOT / 'components/camera')])
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_feature_gates(self):
        for file, call in [('main/noise_control.cpp', 'home_eyes_register(commands)'),
                           ('main/app.c', 'return home_eyes_command(command, params)'),
                           ('components/muse/muse_ui.c', 'muse_home_eyes_tick()'),
                           ('components/muse/muse_input.c', 'muse_home_eyes_dismiss()')]:
            source = (ROOT / file).read_text()
            position = source.index(call)
            stack = []
            for line in source[:position].splitlines():
                if line.startswith(('#if ', '#ifdef ', '#ifndef ')):
                    stack.append(line)
                elif line.startswith('#endif'):
                    stack.pop()
            self.assertIn('#if CONFIG_MUSE_HOME_EYES', stack, file)
        overlay = (ROOT / 'devices/sdkconfig.muse-sensecap-watcher').read_text()
        for option in ('MUSE_HOME_EYES', 'MUSE_WATCHER_CAMERA', 'HOMEHUB_GADGET_EVENTS', 'HOMEHUB_GADGET_CLOCK'):
            self.assertIn(f'CONFIG_{option}=y', overlay)
        kconfig = (ROOT / 'components/muse/Kconfig').read_text().split('config MUSE_HOME_EYES')[1].split('config MUSE_CJK_FONT')[0]
        self.assertIn('depends on MUSE_WATCHER_CAMERA', kconfig)
        self.assertIn('select HOMEHUB_GADGET_EVENTS', kconfig)
        self.assertIn('select HOMEHUB_GADGET_CLOCK', kconfig)


if __name__ == '__main__':
    unittest.main()
