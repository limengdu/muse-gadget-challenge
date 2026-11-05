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

import os
import struct
import zlib
from pathlib import Path
import subprocess
import unittest
import tempfile

try:
    from .card_test_support import ROOT, ENV, compile_host, write_rtos
except ImportError:
    from card_test_support import ROOT, ENV, compile_host, write_rtos


class KidBoard(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        out = Path(cls.temp.name)
        write_rtos(out)
        headers = {
            "sdkconfig.h": "#define CONFIG_SPIRAM 0\n#define CONFIG_HOMEHUB_KID_BOARD 1\n",
            "freertos/queue.h": """#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned,unsigned);
BaseType_t xQueueSend(QueueHandle_t,const void *,TickType_t);
BaseType_t xQueueReceive(QueueHandle_t,void *,TickType_t);
unsigned uxQueueMessagesWaiting(QueueHandle_t);
""",
            "driver/i2c_master.h": "typedef void *i2c_master_bus_handle_t;\n",
            "nvs.h": '#include_next "nvs.h"\nesp_err_t nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);\nesp_err_t nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);\n',
            "esp_log.h": 'void kid_test_log(const char *,const char *,...);\n#define ESP_LOGI(tag, ...) kid_test_log(tag, __VA_ARGS__)\n#define ESP_LOGW(tag, ...) ((void)(tag))\n',
        }
        for name, text in headers.items():
            p = out / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text)
        sources = [ROOT / "tests/kid_board_harness.c"] + [ROOT / "main" / name for name in (
            "kid_model.c", "kid_board_view.c", "kid_store.c", "epaper_cards.c", "card_mask.c", "card_fonts.c", "card_icons.c")]
        cls.binary = compile_host(out, sources, json=True, flags=["-DCONFIG_HOMEHUB_KID_BOARD=1"])

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True, env=ENV)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_validation_and_limits(self): self.run_case("validation")
    def test_current_item_and_date_rollover(self): self.run_case("selection")
    def test_green_key_event_and_star_goal(self): self.run_case("keys")
    def test_nvs_round_trip_and_corruption(self): self.run_case("store")
    def test_refresh_window(self): self.run_case("refresh")
    def test_full_hour_redraws_only_at_item_boundaries(self): self.run_case("hour")
    def test_minute_ticks_and_noops_leave_pixels_unchanged(self): self.run_case("minute")
    def test_midnight_requests_one_redraw(self): self.run_case("midnight")
    def test_async_queue_nvs_and_refresh_coalescing(self):
        result = self.run_case("controller")
        logs = "\n".join(line for line in result.stdout.splitlines() if line.startswith("@kid "))
        self.assertIn("event type=kid.done", logs)
        for text in ("Mia", "Grandpa", "Wake up", "Homework", "Proud of you"):
            self.assertNotIn(text, logs)

    def test_preview_pages(self):
        self.run_case("preview")
        if not os.environ.get("CARD_PREVIEW_DIR"):
            return
        palette = {(0, 0, 0), (255, 255, 255), (255, 255, 0), (255, 0, 0), (0, 0, 255), (0, 255, 0)}
        for name in ("kid_morning", "kid_afternoon_changed", "kid_done", "kid_stars_goal"):
            path = Path(os.environ["CARD_PREVIEW_DIR"]) / (name + ".ppm")
            data = path.read_bytes()
            header = b"P6\n800 480\n255\n"
            self.assertTrue(data.startswith(header))
            self.assertEqual(len(data), len(header) + 800 * 480 * 3)
            pixels = data[len(header):]
            self.assertEqual(set(zip(pixels[::3], pixels[1::3], pixels[2::3])), palette)
            def chunk(kind, value):
                return struct.pack(">I", len(value)) + kind + value + struct.pack(">I", zlib.crc32(kind + value))
            rows = b"".join(b"\0" + pixels[y * 2400:(y + 1) * 2400] for y in range(480))
            path.with_suffix(".png").write_bytes(b"\x89PNG\r\n\x1a\n" +
                chunk(b"IHDR", struct.pack(">IIBBBBB", 800, 480, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
            self.assertTrue(path.with_suffix(".png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))
            def count(color, bounds):
                x0, y0, x1, y1 = bounds
                return sum(tuple(pixels[(y * 800 + x) * 3:(y * 800 + x) * 3 + 3]) == color
                           for y in range(y0, y1) for x in range(x0, x1))
            self.assertGreater(count((0, 0, 255), (0, 0, 800, 72)), 20000)
            if name == "kid_done":
                self.assertGreater(count((0, 255, 0), (45, 100, 125, 170)), 500)
            if name == "kid_stars_goal":
                self.assertGreater(count((0, 255, 0), (530, 375, 785, 440)), 300)

    def test_feature_gates_and_asset_subset(self):
        for filename in ("noise_control.cpp", "app.c"):
            source = (ROOT / "main" / filename).read_text()
            for name in ("kid.set_schedule", "kid.set_stars", "kid.mark", "kid.state"):
                at = source.index('"' + name + '"')
                guard = source.rindex("#if ", 0, at)
                self.assertEqual(source[guard:source.index("\n", guard)], "#if CONFIG_HOMEHUB_KID_BOARD")
                self.assertNotIn("#endif", source[guard:at])
        block = (ROOT / "main/Kconfig.projbuild").read_text().split("config HOMEHUB_KID_BOARD\n")[1].split("config ")[0]
        self.assertIn("depends on HOMEHUB_LED_BACKEND_RETERMINAL_SPECTRA6", block)
        for name in ("GADGET_CLOCK", "GADGET_EVENTS", "EPAPER_CARDS", "RETERMINAL_IO"):
            self.assertIn("select HOMEHUB_" + name, block)
        for overlay in (ROOT / "devices").glob("sdkconfig.*"):
            self.assertEqual("CONFIG_HOMEHUB_KID_BOARD=y" in overlay.read_text(), overlay.name == "sdkconfig.reterminal-e1002")
        self.run_case("assets")


if __name__ == "__main__":
    unittest.main()
