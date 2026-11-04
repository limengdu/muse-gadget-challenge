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

"""Route the production Notes screen through the pinned FreeInk hit table."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest
try:
    from .card_test_support import ROOT, JSON, ENV, SANITIZERS
except ImportError:
    from card_test_support import ROOT, JSON, ENV, SANITIZERS


class StickyNotesEink(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = Path(os.environ.get("FREEINK_SOURCE_DIR", ROOT / "managed_components/freeink-sdk"))
        ui = source / "libs/ui/FreeInkUI"
        if not (ui / "include/FreeInkApp.h").is_file():
            raise AssertionError("Pinned FreeInk source missing: set FREEINK_SOURCE_DIR")
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        out = Path(cls.tmp.name)
        page = (ROOT / "components/muse/eink/muse_eink_notes.cpp").read_text()
        (out / "notes_page.inc").write_text(re.sub(r'^#include.*$', '', page, flags=re.M))
        root = (ROOT / "components/muse/eink/muse_eink_ui.cpp").read_text()
        start = root.index("static void set_page(bool settings)")
        end = root.index("/* Touch events since the last pass,", start)
        (out / "notes_navigation.inc").write_text(root[start:end])
        start = root.index("static void handle_touch(")
        end = root.index("static void ui_task(", start)
        (out / "notes_touch.inc").write_text(root[start:end])
        cc = shlex.split(os.environ.get("CC", "cc"))
        objects = []
        for name, path in (("json", JSON / "cJSON.c"), ("model", ROOT / "main/sticky_notes_model.c")):
            obj = out / f"{name}.o"
            subprocess.run([*cc, "-std=c11", *SANITIZERS, "-Wno-deprecated-declarations", "-I", str(JSON),
                            "-I", str(ROOT / "main"), "-c", str(path), "-o", str(obj)], check=True, capture_output=True)
            objects.append(obj)
        cls.binary = out / "notes"
        command = [*shlex.split(os.environ.get("CXX", "c++")), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                   *SANITIZERS, "-g", "-DCONFIG_MUSE_STICKY_NOTES=1", "-I", str(out), "-I", str(JSON),
                   "-I", str(ROOT / "main"), "-I", str(ui / "include"),
                   str(ROOT / "tests/sticky_notes_eink_harness.cpp"), str(ui / "src/FreeInkUI.cpp"),
                   *map(str, objects), "-o", str(cls.binary)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True, env=ENV)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_tap_row_toggles_and_untoggles(self):
        self.case("toggle")

    def test_remote_toggle_redraws_with_a_black_checkbox_and_checkmark(self):
        self.case("remote_toggle")

    def test_revised_text_and_author_request_a_draw_without_moving_rows(self):
        self.case("revision")

    def test_busy_snapshot_requests_a_retry_and_then_shows_done(self):
        self.case("view_retry")

    def test_swipes_tabs_settings_and_face_remain_reachable(self):
        self.case("lists")

    def test_all_twenty_rows_are_reachable(self):
        self.case("paging")

    def test_clear_done_removes_only_ticked_rows(self):
        self.case("clear")

    def test_reply_completion_preserves_an_open_settings_editor(self):
        self.case("reply_settings")

    def test_voice_reply_returns_to_notes_after_reading(self):
        self.case("reply")


if __name__ == "__main__":
    unittest.main()
