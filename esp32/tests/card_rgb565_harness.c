/*
 * Copyright (c) 2026 limengdu
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "card_rgb565.h"
#include <assert.h>
#include <stdio.h>

// Emits the pixels of full and clipped icons in both asset sizes.
// 输出两种资源尺寸下完整图标与裁切图标的像素。
int main(int argc, char **argv) {
    uint16_t pixels[128 * 128];
    card_rgb565_t canvas;
    assert(argc > 1 && card_rgb565_canvas(&canvas, pixels, sizeof(pixels), 128, 128));
    assert(!card_rgb565_icon(&canvas, 0, 0, "missing", 64, 0xabcd));
    for (int i = 1; i < argc; i++) {
        for (int size = 64; size <= 112; size += 48) {
            for (int clipped = 0; clipped < 2; clipped++) {
                card_rgb565_clear(&canvas, 0x1234);
                int x = clipped ? -size / 3 : 5, y = clipped ? -size / 4 : 3;
                assert(card_rgb565_icon(&canvas, x, y, argv[i], size, 0xabcd));
                for (unsigned p = 0; p < sizeof(pixels) / sizeof(pixels[0]); p++) {
                    assert(pixels[p] == 0x1234 || pixels[p] == 0xabcd);
                    assert(fputc(pixels[p] == 0xabcd, stdout) != EOF);
                }
            }
        }
    }
    return 0;
}
