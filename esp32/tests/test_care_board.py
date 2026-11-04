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
from pathlib import Path
import subprocess
import tempfile
import unittest

try:
    from .card_test_support import ROOT, ENV, compile_host, write_rtos
except ImportError:
    from card_test_support import ROOT, ENV, compile_host, write_rtos


class CareBoard(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        out = Path(cls.temp.name)
        write_rtos(out)
        headers = {
            "sdkconfig.h": "#define CONFIG_SPIRAM 0\n",
            "freertos/queue.h": """#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned,unsigned);
BaseType_t xQueueSend(QueueHandle_t,const void *,TickType_t);
BaseType_t xQueueReceive(QueueHandle_t,void *,TickType_t);
unsigned uxQueueMessagesWaiting(QueueHandle_t);
""",
            "driver/i2c_master.h": "typedef void *i2c_master_bus_handle_t;\n",
            "nvs.h": '#include_next "nvs.h"\nesp_err_t nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);\nesp_err_t nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);\n',
            "esp_log.h": '#define ESP_LOGW(tag, ...) ((void)(tag))\n',
        }
        for name, text in headers.items():
            p = out / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text)
        source = (ROOT / "main/reterminal_io.c").read_text()
        start = source.index("// ---- RTC protocol (host-tested)")
        (out / "rtc_protocol.inc").write_text(source[start:source.index("// ---- Hardware", start)])
        sources = [ROOT / "tests/care_board_harness.c"] + [ROOT / "main" / name for name in (
            "care_model.c", "care_board_view.c", "care_store.c", "epaper_cards.c", "card_fonts.c", "card_icons.c")]
        cls.binary = compile_host(out, sources, json=True)

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True, env=ENV)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_validation_and_limits(self): self.run_case("validation")
    def test_reminder_deadlines_and_rollover(self): self.run_case("engine")
    def test_keys_events_and_footer(self): self.run_case("keys")
    def test_nvs_round_trip(self): self.run_case("store")
    def test_rtc_calendar_and_integrity(self): self.run_case("rtc")
    def test_async_commands_and_display(self): self.run_case("controller")
    def test_preview_pages(self):
        self.run_case("preview")
        if os.environ.get("CARD_PREVIEW_DIR"):
            for name in ("care_day", "care_due", "care_missed"):
                data = (Path(os.environ["CARD_PREVIEW_DIR"]) / (name + ".pgm")).read_bytes()
                self.assertTrue(data.startswith(b"P5\n800 480\n255\n"))
                self.assertEqual(len(data), len(b"P5\n800 480\n255\n") + 800 * 480)
                pixels = data[len(b"P5\n800 480\n255\n"):]
                for x0, y0, x1, y1 in ((18, 4, 400, 90), (18, 273, 115, 301), (18, 441, 787, 477)):
                    ink = sum(pixels[y * 800 + x] == 0 for y in range(y0, y1) for x in range(x0, x1))
                    self.assertGreater(ink, 80)

    def test_feature_gates(self):
        for file, names in (("noise_control.cpp", ("care.update", "care.state", "care.clear")),
                            ("app.c", ("care.update", "care.state", "care.clear"))):
            source = (ROOT / "main" / file).read_text()
            for name in names:
                at = source.index('"' + name + '"')
                guard = source.rindex("#if ", 0, at)
                self.assertEqual(source[guard:source.index("\n", guard)], "#if CONFIG_HOMEHUB_CARE_BOARD")
                self.assertNotIn("#endif", source[guard:at])
        config = (ROOT / "main/Kconfig.projbuild").read_text().split("config HOMEHUB_CARE_BOARD\n")[1].split("config ")[0]
        self.assertIn("depends on HOMEHUB_LED_BACKEND_RETERMINAL_UC8179", config)
        for option in ("HOMEHUB_GADGET_CLOCK", "HOMEHUB_GADGET_EVENTS", "HOMEHUB_EPAPER_CARDS", "HOMEHUB_RETERMINAL_IO"):
            self.assertIn("select " + option, config)


if __name__ == "__main__":
    unittest.main()
