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
    from .card_test_support import ROOT, ENV, compile_host, run_host, write_rtos
except ImportError:
    from card_test_support import ROOT, ENV, compile_host, run_host, write_rtos


class EpaperCards(unittest.TestCase):
    def test_native_page_and_assets(self):
        with tempfile.TemporaryDirectory() as tmp:
            sources = [ROOT / "tests/epaper_cards_harness.c"] + [
                ROOT / "main" / name for name in ("epaper_cards.c", "card_fonts.c", "card_icons.c")]
            binary = compile_host(Path(tmp), sources)
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("ok", result.stdout)
            if os.environ.get("CARD_PREVIEW_DIR"):
                p = Path(os.environ["CARD_PREVIEW_DIR"])
                for name, magic in (("e1001_sample.pgm", b"P5"), ("e1002_sample.ppm", b"P6")):
                    self.assertTrue((p / name).read_bytes().startswith(magic))

    def test_app_page_refresh_and_replacement(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            write_rtos(out)
            (out / "sdkconfig.h").write_text('#define CONFIG_HOMEHUB_EPAPER_CARDS 1\n'
                                            '#define CONFIG_HOMEHUB_DISPLAY 1\n'
                                            '#define CONFIG_HOMEHUB_BUTTON_LABEL "BOOT"\n')
            (out / "esp_err.h").write_text('typedef int esp_err_t;\n#define ESP_OK 0\n'
                                         'static inline const char *esp_err_to_name(int e) {(void)e;return "fake";}\n')
            (out / "esp_log.h").write_text('#define ESP_LOGI(tag, ...) ((void)(tag))\n'
                                         '#define ESP_LOGW(tag, ...) ((void)(tag))\n'
                                         '#define ESP_LOGE(tag, ...) ((void)(tag))\n')
            (out / "esp_heap_caps.h").write_text('#include_next "esp_heap_caps.h"\n'
                'static inline void *heap_caps_calloc(size_t n,size_t s,int c) {(void)c;return calloc(n,s); }\n')
            sources = [ROOT / "tests/epaper_cards_status_harness.c"] + [
                ROOT / "main" / name for name in ("epaper_cards.c", "card_fonts.c", "card_icons.c")]
            binary = compile_host(out, sources, flags=["-I", str(ROOT / "avatar")])
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_assets_are_conditional(self):
        cmake = (ROOT / "main/CMakeLists.txt").read_text()
        block = cmake.split("if(CONFIG_HOMEHUB_EPAPER_CARDS)", 1)[1].split("endif()", 1)[0]
        for source in ("epaper_cards.c", "card_fonts.c", "card_icons.c"):
            self.assertIn(source, block)
        kconfig = (ROOT / "main/Kconfig.projbuild").read_text()
        block = kconfig.split("config HOMEHUB_EPAPER_CARDS\n", 1)[1].split("config ", 1)[0]
        self.assertIn("default n", block)
        self.assertIn("RETERMINAL_UC8179 || HOMEHUB_LED_BACKEND_RETERMINAL_SPECTRA6", block)


if __name__ == "__main__":
    unittest.main()
