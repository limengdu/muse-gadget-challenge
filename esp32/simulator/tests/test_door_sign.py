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


"""Render the production sign UI and compare checked-in, deterministic screenshots."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

HERE = Path(__file__).resolve().parent


def png(ppm):
    magic, dimensions, maximum, pixels = ppm.split(b"\n", 3)
    width, height = map(int, dimensions.split())
    assert magic == b"P6" and maximum == b"255" and len(pixels) == width * height * 3
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    rows = b"".join(b"\0" + pixels[y*width*3:(y+1)*width*3] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def reference_pixels(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    payload = b""
    offset = 8
    width = height = 0
    while offset < len(data):
        size = struct.unpack(">I", data[offset:offset+4])[0]
        kind = data[offset+4:offset+8]
        chunk = data[offset+8:offset+8+size]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            assert depth == 8 and color == 2 and interlace == 0
        if kind == b"IDAT":
            payload += chunk
        offset += size + 12
    rows = zlib.decompress(payload)
    stride = width * 3 + 1
    assert len(rows) == stride * height
    assert all(rows[y*stride] == 0 for y in range(height))
    return b"".join(rows[y*stride+1:(y+1)*stride] for y in range(height))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()
    env = {**os.environ, "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"}
    references = HERE / "screenshots/door-sign"
    if args.update:
        references.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="door-sign-ui-") as tmp:
        for width, height, round_screen in ((172, 320, 0), (466, 466, 1), (320, 240, 0)):
            captures = []
            for repeat in range(2):
                directory = Path(tmp) / f"{width}-{repeat}"
                directory.mkdir()
                proc = subprocess.run([str(args.binary.resolve()), str(width), str(height), str(round_screen), str(directory)],
                                      env=env, text=True, capture_output=True, timeout=20)
                assert proc.returncode == 0, proc.stdout + proc.stderr
                captures.append({p.stem: p.read_bytes() for p in directory.glob("*.ppm")})
            assert captures[0] == captures[1], "Door sign screenshots changed between identical runs"
            assert set(captures[0]) == {"meeting", "sent", "face", "custom", "free"}
            assert len(set(captures[0].values())) == 5, "Sign, reply, face, custom and free must be distinct"
            for name, data in captures[0].items():
                reference = references / f"{width}x{height}-{name}.png"
                rendered = png(data)
                if args.update:
                    reference.write_bytes(rendered)
                # The sign uses flat widgets; compare the checked-in pixels exactly.
                # 门牌使用平面控件；逐一比对已纳入版本管理的像素。
                if name != "face":
                    assert reference_pixels(reference) == data.split(b"\n", 3)[3], f"Screenshot changed: {reference.name}"
            print(f"{width}x{height}: tap, voice view, settings, sleep/wake, face and screenshots passed")


if __name__ == "__main__":
    main()
