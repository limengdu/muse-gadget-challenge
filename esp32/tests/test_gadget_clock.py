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
import unittest

try:
    from .card_test_support import ROOT, compile_host, run_host, write_rtos
except ImportError:
    from card_test_support import ROOT, compile_host, run_host, write_rtos


class GadgetClock(unittest.TestCase):
    def test_time_offset_persistence_and_sntp(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            write_rtos(out)
            (out / "esp_netif_sntp.h").write_text("""#pragma once
typedef int esp_err_t;
#define ESP_OK 0
typedef struct {bool start, wait_for_sync; const char *servers[1];} esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) {.start=true,.wait_for_sync=true,.servers={server}}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *);
""")
            # The log consumes its tag while the host fake discards formatting.
            (out / "esp_log.h").write_text('#define ESP_LOGI(tag, ...) ((void)(tag))\n')
            binary = compile_host(out, [ROOT / "tests/gadget_clock_harness.c"], json=True)
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
