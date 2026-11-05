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

import json
from pathlib import Path
import tempfile
import unittest

try:
    from .card_test_support import ROOT, compile_host, run_host, write_rtos
except ImportError:
    from card_test_support import ROOT, compile_host, run_host, write_rtos


class GadgetEvents(unittest.TestCase):
    def test_silent_turn_and_fifo_delivery(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            write_rtos(out)
            (out / "sdkconfig.h").write_text("#define CONFIG_SPIRAM 0\n")
            binary = compile_host(out, [ROOT / "tests/gadget_events_silent_harness.c"], json=True,
                flags=[f'-DREPLY_FIXTURE_PATH="{ROOT / "tests/gadget_reply_fixture.json"}"',
                       f'-DSILENT_FIXTURE_PATH="{ROOT / "tests/gadget_silent_fixture.json"}"'])
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            progress = [json.loads(line[7:]) for line in result.stdout.splitlines() if line.startswith("@event ")]
            self.assertEqual([line["status"] for line in progress], ["queued", "sent", "delivered"])
            self.assertEqual(progress[-1], {"status": "delivered", "seq": 1})
            states = [json.loads(line[len("@event.state "):]) for line in result.stdout.splitlines() if line.startswith("@event.state ")]
            self.assertEqual(len(states), 1)
            self.assertEqual(states[0]["reason"], "silent")
            self.assertEqual(states[0]["quiet_ms"], 4000)
            self.assertEqual(states[0]["count"], 0)
            self.assertFalse(states[0]["complete"] or states[0]["agent_busy"])

    def test_queue_stream_retry_and_poll(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            write_rtos(out)
            (out / "sdkconfig.h").write_text("#define CONFIG_SPIRAM 0\n")
            binary = compile_host(out, [ROOT / "tests/gadget_events_harness.c"], json=True,
                flags=['-DREPLY_FIXTURE_PATH="'+str(ROOT / "tests/gadget_reply_fixture.json")+'"'])
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            progress = [json.loads(line[7:]) for line in result.stdout.splitlines() if line.startswith("@event ")]
            self.assertEqual([line["status"] for line in progress], ["queued", "sent", "reply"])
            self.assertEqual(progress[-1]["text"], "All ready!")
            self.assertNotIn("PRIVATE_", result.stdout)
            debug = [json.loads(line[len("@event.debug "):]) for line in result.stdout.splitlines() if line.startswith("@event.debug ")]
            self.assertEqual(debug, [{"enabled": True}, {"enabled": False}])
            rows = [json.loads(line[len("@event.row "):]) for line in result.stdout.splitlines() if line.startswith("@event.row ")]
            self.assertTrue(rows)
            self.assertTrue(any(row["len"] > 12288 and row["keys"] == [] for row in rows))
            self.assertTrue(any(row["ids"].get("payload.message_id") == "message-id" for row in rows))
            for row in rows:
                self.assertEqual(set(row) - {"dropped"}, {"req", "status", "len", "keys", "type", "event", "event_name", "payload_keys", "ids", "seq", "activity_code", "payload_status"})
                self.assertTrue(row["seq"] is None or isinstance(row["seq"], (int, float)))
                for key in ("activity_code", "payload_status"):
                    self.assertTrue(row[key] is None or isinstance(row[key], str))
            sequenced = [row for row in rows if row["seq"] == 77]
            self.assertEqual(len(sequenced), 2)
            self.assertNotIn("dropped", sequenced[0])
            self.assertEqual(sequenced[1]["dropped"], "seq")
            self.assertEqual(sequenced[0]["activity_code"], "thinking")
            self.assertEqual(sequenced[0]["payload_status"], "running")
            self.assertTrue(any(row["event"] == "task.status" and row["payload_status"] == "running" for row in rows))
            diagnostic = next(row for row in rows if row["event"] == "diag.event")
            self.assertIsNone(diagnostic["seq"])
            self.assertIsNone(diagnostic["activity_code"])
            self.assertIsNone(diagnostic["payload_status"])
            frames = [json.loads(line[len("@event.frame "):]) for line in result.stdout.splitlines() if line.startswith("@event.frame ")]
            self.assertEqual(len(frames), 5)
            self.assertIn({"req": "sub", "bytes": 7, "newlines": 1, "pending": 0, "ws_only": True}, frames)
            self.assertIn({"req": "sub", "bytes": 1, "newlines": 0, "pending": 1, "ws_only": False}, frames)
            for frame in frames:
                self.assertEqual(set(frame), {"req", "bytes", "newlines", "pending", "ws_only"})
                self.assertEqual(frame["req"], "sub")
                self.assertIsInstance(frame["ws_only"], bool)
            states = [json.loads(line[len("@event.state "):]) for line in result.stdout.splitlines() if line.startswith("@event.state ")]
            self.assertEqual([state["reason"] for state in states], ["settled", "timeout", "failed", "failed"])
            settled = states[0]
            self.assertTrue(settled["note_id_set"] and settled["complete"] and settled["agent_busy"])
            self.assertEqual(settled["echo_row"], 1)
            self.assertEqual(settled["row"], 5)
            self.assertEqual(settled["count"], 1)
            self.assertEqual(settled["quiet_ms"], 20000)
            self.assertEqual(settled["content_age_ms"], 20000)
            self.assertEqual(settled["candidates"], [{"id_tail": "msg-turn", "started": 2, "done": True, "text_len": 10}])
            self.assertTrue(states[1]["note_id_set"])
            self.assertFalse(states[1]["complete"])
            for state in states:
                self.assertEqual(set(state), {"reason", "note_id_set", "echo_row", "row", "server_seq", "count", "candidates", "complete", "agent_busy", "pending", "overflow", "quiet_ms", "content_age_ms"})
                self.assertEqual(state["pending"], 0)
                self.assertFalse(state["overflow"])
                for candidate in state["candidates"]:
                    self.assertEqual(set(candidate), {"id_tail", "started", "done", "text_len"})
                    self.assertLessEqual(len(candidate["id_tail"]), 8)
            for line in result.stdout.splitlines():
                if line.startswith("@event.request "): json.loads(line[len("@event.request "):])

    def test_psram_allocation_and_no_internal_fallback_when_present(self):
        for present, watcher in ((0, 0), (1, 0), (0, 1), (1, 1)):
            with self.subTest(psram=present, watcher=watcher), tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp)
                write_rtos(out)
                (out / "sdkconfig.h").write_text(f"#define CONFIG_SPIRAM 1\n#define CONFIG_MUSE_BOARD_SENSECAP_WATCHER {watcher}\n")
                (out / "esp_psram.h").write_text("#include <stdbool.h>\nbool esp_psram_is_initialized(void);\n")
                binary = compile_host(out, [ROOT / "tests/gadget_events_harness.c"], json=True,
                    flags=['-DREPLY_FIXTURE_PATH="'+str(ROOT / "tests/gadget_reply_fixture.json")+'"', "-DLINK_FAKE_CUSTOM_CALLOC=1", f"-DTEST_PSRAM_AVAILABLE={present}"])
                result = run_host(binary)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_console_token_and_extension_dispatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            write_rtos(out)
            headers = {
                "sdkconfig.h": '#define CONFIG_HOMEHUB_GADGET_EVENTS 1\n#define CONFIG_ESP_CONSOLE_UART 1\n#define CONFIG_ESP_CONSOLE_UART_NUM 0\n',
                "soc/soc_caps.h": '#define SOC_USB_SERIAL_JTAG_SUPPORTED 0\n',
                "driver/uart.h": '#define ESP_OK 0\nint uart_driver_install(int,int,int,int,void *,int);\nint uart_read_bytes(int,void *,unsigned,int);\n',
                "driver/uart_vfs.h": 'void uart_vfs_dev_use_driver(int);\n',
                "esp_system.h": 'void esp_restart(void);\n',
                "esp_log.h": '#define ESP_LOGI(tag, ...) ((void)(tag))\n#define ESP_LOGW(tag, ...) ((void)(tag))\n#define ESP_LOGE(tag, ...) ((void)(tag))\n',
            }
            for name, text in headers.items():
                p = out / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(text)
            binary = compile_host(out, [ROOT / "tests/gadget_console_harness.c"])
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_command_gate_and_dispatch(self):
        for name, marker in (("noise_control.cpp", 'add_command(commands, "events.poll"'),
                             ("app.c", 'strcmp(command, "events.poll")')):
            source = (ROOT / "main" / name).read_text()
            at = source.index(marker)
            guard = source.rindex("#if ", 0, at)
            self.assertEqual(source[guard:source.index("\n", guard)], "#if CONFIG_HOMEHUB_GADGET_EVENTS")
            self.assertNotIn("#endif", source[guard:at])
        kconfig = (ROOT / "main/Kconfig.projbuild").read_text()
        block = kconfig.split("config HOMEHUB_GADGET_EVENTS\n", 1)[1].split("config ", 1)[0]
        self.assertIn("depends on HOMEHUB_DAEMON_REQUESTS", block)
        self.assertIn("default n", block)

    def test_registration_fits_e1001_and_e1002_with_features_on_and_off(self):
        source = (ROOT / "main/noise_control.cpp").read_text()
        begin = source.index("static cJSON *string_param(")
        end = source.index("// ---- OTA callback", begin)
        builder = source[begin:end]
        for bits in (1, 4):
            for enabled in (0, 1):
                with self.subTest(bits=bits, enabled=enabled), tempfile.TemporaryDirectory() as tmp:
                    out = Path(tmp)
                    harness = out / "registration.cpp"
                    harness.write_text("""#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "cJSON.h"
}
static char s_wifi_ssid[33] = "home", s_register_req_id[40];
static const char *s_node_id = "node", *s_display_name = "Muse gadget";
static void copy_wifi_ssid(char *out, size_t n) {snprintf(out, n, "%s", s_wifi_ssid);}
static void make_uuid(char *out, size_t n) {snprintf(out, n, "00000000-0000-0000-0000-000000000000");}
struct esp_app_desc_t {const char *version;};
static const esp_app_desc_t *esp_app_get_description() {static esp_app_desc_t d={"999.0.0"};return &d;}
static bool led_status_display_info(int *w, int *h) {*w=800;*h=480;return true;}
static int led_status_display_bits() {return """ + str(bits) + ";}\n" + """
static bool ota_is_enabled() {return true;}
""" + builder + """
int main() {
    char *json=build_register_json(); assert(json && strlen(json)<8192);
    cJSON *root=cJSON_Parse(json);
    cJSON *commands=cJSON_GetObjectItem(cJSON_GetObjectItem(root,"params"),"commands_v2");
    assert((cJSON_GetObjectItem(commands,"events.poll")!=nullptr)==CONFIG_HOMEHUB_GADGET_EVENTS);
    assert((cJSON_GetObjectItem(commands,"clock.set")!=nullptr)==CONFIG_HOMEHUB_GADGET_CLOCK);
    assert((cJSON_GetObjectItem(commands,"care.update")!=nullptr)==CONFIG_HOMEHUB_CARE_BOARD);
    assert((cJSON_GetObjectItem(commands,"care.state")!=nullptr)==CONFIG_HOMEHUB_CARE_BOARD);
    assert((cJSON_GetObjectItem(commands,"care.clear")!=nullptr)==CONFIG_HOMEHUB_CARE_BOARD);
    printf("%zu bytes\\n",strlen(json));
    free(json);cJSON_Delete(root);
}
""")
                    # Compile cJSON as C, then link the production C++ builder.
                    obj = out / "json.o"
                    import os, shlex, subprocess
                    try:
                        from .card_test_support import JSON
                    except ImportError:
                        from card_test_support import JSON
                    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-c", str(JSON / "cJSON.c"),
                                    "-o", str(obj)], check=True, capture_output=True)
                    flags = [f"-DCONFIG_HOMEHUB_CARE_BOARD={enabled if bits == 1 else 0}", f"-DCONFIG_HOMEHUB_GADGET_EVENTS={enabled}", f"-DCONFIG_HOMEHUB_GADGET_CLOCK={enabled}",
                             "-DCONFIG_HOMEHUB_DISPLAY_COMMANDS=1", "-DCONFIG_HOMEHUB_RETERMINAL_SHT4X=1",
                             "-DCONFIG_HOMEHUB_TUNNEL=1"]
                    binary = compile_host(out, [harness, obj], flags=flags, cxx=True)
                    result = run_host(binary)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    print(f"registration bpp={bits} features={enabled}: {result.stdout.strip()}")


if __name__ == "__main__":
    unittest.main()
