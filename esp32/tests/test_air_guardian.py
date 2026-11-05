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


import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

try:
    from .card_test_support import ROOT, ENV, JSON, compile_host, run_host, write_rtos
except ImportError:
    from card_test_support import ROOT, ENV, JSON, compile_host, run_host, write_rtos


class AirGuardian(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.out = Path(cls.tmp.name)
        cls.binary = compile_host(cls.out, [ROOT / "tests/air_guardian_harness.c"] + [
            ROOT / "main" / name for name in ("air_guardian_core.c", "air_guardian_render.c",
                                              "card_rgb565.c", "card_mask.c", "card_fonts.c", "card_icons.c")], json=True)

    def check(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True, env=ENV)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        if case == "size":
            print(result.stdout.strip())

    def test_complete_snapshot_validation_and_atomic_update(self):
        self.check("validation")

    def test_aqi_categories_and_optional_pm25(self):
        self.check("boundaries")

    def test_versioned_record_restore_and_failed_save(self):
        self.check("persistence")

    def test_three_views(self):
        self.check("views")

    def test_status_age_missing_clock_and_refresh(self):
        self.check("status")

    def test_native_previews(self):
        reference = json.loads((ROOT / "tests/card_assets_reference.json").read_text())
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(os.environ.get("CARD_PREVIEW_DIR", tmp))
            result = subprocess.run([str(self.binary), "previews"], capture_output=True, text=True,
                                    env={**ENV, "CARD_PREVIEW_DIR": str(directory)})
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for view in ("good", "moderate", "unhealthy", "night"):
                path = directory / f"outdoor_{view}.ppm"
                data = path.read_bytes()
                self.assertTrue(data.startswith(b"P6\n480 480\n255\n"))
                self.assertEqual(len(data), 480 * 480 * 3 + 15)
                self.assertEqual(hashlib.sha256(data).hexdigest(), reference["previews"][path.name])

    def test_rgb565_icons_match_uncompressed_reference(self):
        reference = json.loads((ROOT / "tests/card_assets_reference.json").read_text())
        names = re.findall(r'static const char name_\w+\[\] = "([^"]+)";',
                           (ROOT / "main/card_icons.c").read_text())
        with tempfile.TemporaryDirectory() as tmp:
            sources = [ROOT / "tests/card_rgb565_harness.c"] + [ROOT / "main" / name
                for name in ("card_rgb565.c", "card_mask.c", "card_fonts.c", "card_icons.c")]
            binary = compile_host(Path(tmp), sources)
            result = subprocess.run([str(binary), *names], capture_output=True, env=ENV)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual(hashlib.sha256(result.stdout).hexdigest(), reference["rgb565_icons_sha256"])

    def test_worker_queue_nvs_restore_and_session_reply(self):
        for psram in (0, 1):
            with self.subTest(psram=psram):
                self.check_worker(psram)

    def check_worker(self, psram):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            write_rtos(out)
            (out / "sdkconfig.h").write_text(f"#define CONFIG_SPIRAM {psram}\n")
            (out / "freertos/FreeRTOS.h").write_text('''#pragma once
#include <stdint.h>
#define configTICK_RATE_HZ 1000
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
typedef int BaseType_t;
typedef uint32_t TickType_t;
''')
            (out / "esp_log.h").write_text('''#pragma once
void air_test_log(const char *, const char *, ...);
#define ESP_LOGI(tag, fmt, ...) air_test_log(tag, fmt, __VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) ((void)0)
#define ESP_LOGE(tag, fmt, ...) ((void)0)
''')
            (out / "freertos/queue.h").write_text("""#pragma once
#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned, unsigned);
void vQueueDelete(QueueHandle_t);
BaseType_t xQueueSend(QueueHandle_t, const void *, TickType_t);
BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t);
""")
            sources = [ROOT / "tests/air_guardian_runtime_harness.c"] + [
                ROOT / "main" / name for name in ("air_guardian_core.c", "air_guardian_render.c",
                                                  "card_rgb565.c", "card_mask.c", "card_fonts.c", "card_icons.c")]
            binary = compile_host(out, sources, json=True, flags=["-DCONFIG_HOMEHUB_AIR_GUARDIAN=1"])
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_indicator_draw_buffers_use_internal_ram(self):
        source = (ROOT / "main/led_status.c").read_text()
        start = source.index("// Draw buffers are copied into the frame buffer by the CPU.")
        config = source[start:source.index("#elif CONFIG_HOMEHUB_LED_BACKEND_WAVESHARE_C6_ST7789", start)]
        for enabled, psram in ((0, 0), (0, 1), (1, 0), (1, 1)):
            with self.subTest(outdoor_air=enabled, psram=psram), tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp)
                harness = out / "caps.c"
                harness.write_text('#include "esp_heap_caps.h"\n' + config + '\nint main(void) {\n'
                    + '    return LCD_BUF_CAPS != (MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);\n'
                    + '}\n')
                binary = compile_host(out, [harness], flags=[f"-DCONFIG_HOMEHUB_AIR_GUARDIAN={enabled}",
                                                           f"-DCONFIG_SPIRAM={psram}"])
                result = run_host(binary)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_lcd_status_image_and_dashboard_ownership(self):
        try:
            from .test_link_ota import function_source
        except ImportError:
            from test_link_ota import function_source
        source = (ROOT / "main/led_status.c").read_text()
        source = source[source.index("static esp_lcd_panel_handle_t s_panel"):]
        functions = "\n".join(function_source(source, signature) for signature in (
            "void led_status_set_state(", "static void lcd_air_sync_state(", "static bool lcd_draw_image_rect(",
            "void led_status_show_animation(", "bool led_status_air_active(", "bool led_status_air_draw(",
            "static void lcd_draw_dot(", "static void led_hw_set_color(", "static void led_hw_set_connected("))
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            harness = out / "lcd.c"
            harness.write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include "led_status.h"
#define portMAX_DELAY (-1)
#define LCD_H_RES 480
#define LCD_V_RES 480
#define LCD_BAR_ROWS 24
#define LCD_IMAGE_MAX_ROWS 23
#define LCD_DOT_SIZE 1
#define LCD_DOT_X 450
#define LCD_DOT_Y 30
typedef struct { uint8_t r,g,b; } rgb_t;
static rgb_t s_bar_color;
static uint16_t s_bar_px,s_bar_buf[LCD_H_RES*LCD_BAR_ROWS];
static uint16_t dot_px(void) {return 0xffff;}
static bool dot_cell_set(int x,int y) {(void)x;(void)y;return true;}
static uint8_t to_full_scale(uint8_t v) {return v;}
static uint16_t lcd_px(rgb_t c) {(void)c;return 0xffff;}
static const rgb_t COLOR_OFF={0,0,0};
static void *s_panel=(void *)1,*s_mutex=(void *)2,*s_lcd_lock=(void *)1;
static atomic_bool s_air_ready,s_air_online;
static bool s_air_drawn,s_air_was_ready,s_image_mode,s_title_dirty,s_bars_drawn,s_dot_drawn;
static led_state_t s_state;
static uint16_t s_anim_buf[480*23];
static uint16_t frame[480*480];
static int clears,draws,yields,wakes,next_y,fail_at;
static bool locked,interrupt_draw;
static void xSemaphoreTake(void *p,int wait) {
    (void)wait;if(p==s_lcd_lock){assert(!locked);locked=true;}
}
static void xSemaphoreGive(void *p) {if(p==s_lcd_lock){assert(locked);locked=false;}}
static void air_guardian_display_changed(void) {assert(!locked);wakes++;}
static void vTaskDelay(int ticks) {
    assert(ticks==1 && locked);yields++;
    if(interrupt_draw) atomic_store(&s_air_ready,false);
}
static void lcd_clear_rows(int a,int b) {assert(a==0 && b==480);clears++;}
static bool lcd_draw(int a,int b,int x,int y,const uint16_t *p) {
    assert(locked && p==s_anim_buf);draws++;
    if(x==480){
        assert(a==0 && b==next_y && y>b && y-b<=LCD_IMAGE_MAX_ROWS);
        assert(!memcmp(p,frame+b*480,(size_t)(y-b)*480*sizeof(uint16_t)));next_y=y;
    }
    return draws!=fail_at;
}
static void lcd_overlay_rect(uint16_t *p,int x,int y,int w,int h) {(void)p;(void)x;(void)y;(void)w;(void)h;}
''' + functions + r'''
static bool dashboard(void) {next_y=0;return led_status_air_draw(frame);}
int main(void) {
    uint16_t pixel=0x3412;
    for(int i=0;i<480*480;i++)frame[i]=(uint16_t)i;
    assert(!led_status_air_active() && !dashboard() && draws==0);
    led_status_set_state(LED_STATE_WS_CONNECTED);lcd_air_sync_state();assert(s_air_ready);
    assert(wakes==1 && led_status_air_active());lcd_air_sync_state();assert(wakes==1);
    assert(dashboard() && s_air_drawn && draws==21 && next_y==480 && yields==20);
    s_air_was_ready=false;lcd_air_sync_state();assert(wakes==1);
    led_hw_set_connected();led_hw_set_color((rgb_t){1,2,3});lcd_draw_dot(true);
    assert(draws==21 && !s_bars_drawn && !s_dot_drawn);
    assert(lcd_draw_image_rect(0,0,1,1,&pixel));
    assert(s_image_mode && !s_air_drawn && s_anim_buf[0]==0x1234);
    assert(!led_status_air_active() && !dashboard());
    led_status_show_animation();assert(!s_image_mode && s_title_dirty);
    assert(wakes==2 && dashboard() && next_y==480);
    led_status_show_animation();assert(wakes==2);
    assert(lcd_draw_image_rect(0,0,1,1,&pixel));
    led_status_set_state(LED_STATE_WS_DISCONNECTED);lcd_air_sync_state();
    assert(s_air_ready && !s_air_online && !s_image_mode);
    assert(wakes==3 && dashboard());
    assert(!lcd_draw_image_rect(0,0,1,1,&pixel) && !s_image_mode);
    led_status_set_state(LED_STATE_WIFI_CONNECTING);lcd_air_sync_state();assert(dashboard() && wakes==3);
    led_status_set_state(LED_STATE_PAIRING_CONFIRM_REQUIRED);lcd_air_sync_state();assert(!s_air_ready && !s_air_drawn);
    assert(!led_status_air_active() && !dashboard());
    led_status_set_state(LED_STATE_ERROR);lcd_air_sync_state();assert(!s_air_ready);
    led_status_set_state(LED_STATE_WS_CONNECTED);lcd_air_sync_state();assert(dashboard() && wakes==4);
    assert(clears==3);
    fail_at=draws+2;assert(!dashboard() && !s_air_drawn && !locked);fail_at=0;
    assert(dashboard() && next_y==480);
    interrupt_draw=true;int before=draws;
    assert(!dashboard() && draws==before+1 && !s_air_drawn && !locked);
    return 0;
}
''')
            binary = compile_host(out, [harness], flags=["-DCONFIG_HOMEHUB_AIR_GUARDIAN=1",
                                                       "-DCONFIG_HOMEHUB_DISPLAY=1"])
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_button_retains_pairing_setup_and_reset(self):
        source = (ROOT / "main/app.c").read_text()
        start = source.index("static void on_button_short_press(")
        block = source[start:source.index("static void on_button_double_press(", start)]
        self.assertLess(block.index("link_pairing_confirmation_required()"), block.index("air_guardian_cycle_view()"))
        self.assertLess(block.index("!config_setup_complete()"), block.index("air_guardian_cycle_view()"))
        self.assertIn('open_setup_window("button short-press")', block)
        self.assertIn('reset_setup_from_control("button long-press")', source)

    def test_outdoor_app_has_no_sensor_or_event_dependency(self):
        source = (ROOT / "main/air_guardian.c").read_text()
        for text in ("sensecap_sensors", "sensors.read", "gadget_events", "air_core_observe", "history"):
            self.assertNotIn(text, source)
        config = (ROOT / "main/Kconfig.projbuild").read_text()
        block = config.split("config HOMEHUB_AIR_GUARDIAN\n", 1)[1].split("config ", 1)[0]
        self.assertNotIn("HOMEHUB_SENSECAP_SENSORS", block)

    def test_assets_and_board_option(self):
        cmake = (ROOT / "main/CMakeLists.txt").read_text()
        block = cmake.split("if(CONFIG_HOMEHUB_CARD_ASSETS)", 1)[1].split("endif()", 1)[0]
        self.assertIn('"card_fonts.c" "card_icons.c"', block)
        config = (ROOT / "main/Kconfig.projbuild").read_text()
        block = config.split("config HOMEHUB_AIR_GUARDIAN\n", 1)[1].split("config ", 1)[0]
        for text in ("default n", "HOMEHUB_LED_BACKEND_SENSECAP_ST7701", "select HOMEHUB_CARD_ASSETS",
                     "select HOMEHUB_GADGET_CLOCK", "SenseCAP Indicator D1"):
            self.assertIn(text, block)
        overlays = list((ROOT / "devices").glob("sdkconfig.*"))
        enabled = [p.name for p in overlays if "CONFIG_HOMEHUB_AIR_GUARDIAN=y" in p.read_text()]
        self.assertEqual(enabled, ["sdkconfig.sensecap-indicator"])

    def test_advertisement_dispatch_and_register_bound(self):
        noise = (ROOT / "main/noise_control.cpp").read_text()
        app = (ROOT / "main/app.c").read_text()
        commands = ("air.update", "air.status", "air.set_view")
        for command in commands:
            for source, marker in ((noise, f'add_command(commands, "{command}"'),
                                   (app, f'"{command}"')):
                start = source.index(marker)
                guard = source.rindex("#if ", 0, start)
                self.assertEqual(source[guard:source.index("\n", guard)], "#if CONFIG_HOMEHUB_AIR_GUARDIAN")
                self.assertNotIn("#endif", source[guard:start])
        builder = noise[noise.index("static cJSON *string_param("):noise.index("// ---- OTA callback")]
        dispatch_start = app.index('#if CONFIG_HOMEHUB_AIR_GUARDIAN', app.index('static cJSON *on_ws_command('))
        dispatch = app[dispatch_start:app.index('#endif', dispatch_start) + len('#endif')]
        for enabled in (0, 1):
            with self.subTest(enabled=enabled), tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp)
                source = out / "register.cpp"
                source.write_text(r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cJSON.h"
static char s_wifi_ssid[33] = "home", s_register_req_id[40];
static const char *s_node_id = "node", *s_display_name = "Muse gadget";
static void copy_wifi_ssid(char *out, size_t n) {snprintf(out,n,"%s",s_wifi_ssid);}
static void make_uuid(char *out, size_t n) {snprintf(out,n,"00000000-0000-0000-0000-000000000000");}
struct esp_app_desc_t { const char *version; };
static const esp_app_desc_t *esp_app_get_description() {static esp_app_desc_t d={"999.0.0"};return &d;}
static bool led_status_display_info(int *w,int *h) {*w=480;*h=480;return true;}
static int led_status_display_bits() {return 16;}
static bool ota_is_enabled() {return true;}
#if CONFIG_HOMEHUB_AIR_GUARDIAN
static cJSON *air_guardian_command(const char *,cJSON *,const char *,int) {return cJSON_CreateTrue();}
#endif
''' + builder + r'''
static cJSON *dispatch(const char *command,cJSON *params,const char *request_id,int session_generation) {
    (void)command; (void)params; (void)request_id; (void)session_generation;
''' + dispatch + r'''
    return nullptr;
}
int main() {
    char *json=build_register_json(); assert(json && strlen(json)<8192);
    cJSON *root=cJSON_Parse(json);
    cJSON *commands=cJSON_GetObjectItem(cJSON_GetObjectItem(root,"params"),"commands_v2");
    const char *names[]={"air.update","air.status","air.set_view"};
    for (auto name:names) {
        assert((cJSON_GetObjectItem(commands,name)!=nullptr)==CONFIG_HOMEHUB_AIR_GUARDIAN);
        cJSON *r=dispatch(name,nullptr,"req",1);
        assert((r!=nullptr)==CONFIG_HOMEHUB_AIR_GUARDIAN); cJSON_Delete(r);
    }
    assert(cJSON_GetObjectItem(commands,"sensors.history")==nullptr);
    assert(cJSON_GetObjectItem(commands,"air.set_thresholds")==nullptr);
    assert((cJSON_GetObjectItem(commands,"sensors.read")==nullptr)==CONFIG_HOMEHUB_AIR_GUARDIAN);
    printf("registration Outdoor Air=%d: %zu bytes\n",CONFIG_HOMEHUB_AIR_GUARDIAN,strlen(json));
    cJSON_Delete(root);free(json);
}
''')
                obj = out / "json.o"
                subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-c", str(JSON / "cJSON.c"),
                                "-o", str(obj)], check=True, capture_output=True)
                flags = [f"-DCONFIG_HOMEHUB_AIR_GUARDIAN={enabled}",
                         f"-DCONFIG_HOMEHUB_GADGET_EVENTS={enabled}", f"-DCONFIG_HOMEHUB_GADGET_CLOCK={enabled}",
                         "-DCONFIG_HOMEHUB_SENSECAP_SENSORS=1", "-DCONFIG_HOMEHUB_DISPLAY_COMMANDS=1",
                         "-DCONFIG_HOMEHUB_TUNNEL=1"]
                binary = compile_host(out, [source, obj], flags=flags, cxx=True)
                result = run_host(binary)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                print(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
