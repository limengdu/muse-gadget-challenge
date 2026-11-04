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

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t *pixels;
    int width, height;
} card_rgb565_t;

bool card_rgb565_canvas(card_rgb565_t *c, uint16_t *pixels, size_t bytes, int w, int h);
uint16_t card_rgb565_color(unsigned r, unsigned g, unsigned b);
void card_rgb565_clear(card_rgb565_t *c, uint16_t color);
void card_rgb565_rect(card_rgb565_t *c, int x, int y, int w, int h, uint16_t color);
void card_rgb565_line(card_rgb565_t *c, int x, int y, int x1, int y1, uint16_t color);
void card_rgb565_ring(card_rgb565_t *c, int x, int y, int radius, int thickness, uint16_t color);
int card_rgb565_measure(const char *text, int size);
// Draws one line fitted within width, using an ellipsis for remaining text.
// 在指定宽度内绘制单行文本，并以省略号表示剩余内容。
void card_rgb565_text(card_rgb565_t *c, int x, int y, int width, const char *text,
                     int size, uint16_t color);
bool card_rgb565_icon(card_rgb565_t *c, int x, int y, const char *name, int size, uint16_t color);
