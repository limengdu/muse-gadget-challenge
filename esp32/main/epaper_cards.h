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
#include <stddef.h>
#include <stdint.h>
#include "card_fonts.h"
#include "card_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CARD_BLACK = 0, CARD_WHITE = 1, CARD_YELLOW = 2,
    CARD_RED = 3, CARD_BLUE = 5, CARD_GREEN = 6,
} card_color_t;

typedef struct {
    uint8_t *pixels;
    int width, height, bits_per_pixel, stride;
} epaper_cards_canvas_t;

typedef struct {
    int lines;
    bool truncated;
} epaper_cards_text_result_t;

// Wraps caller-owned panel bytes: MSB-first mono, or high-nibble-first ink codes.
// 包装调用方持有的面板缓冲区：单色高位优先，彩色高半字节优先。
bool epaper_cards_canvas(epaper_cards_canvas_t *canvas, uint8_t *pixels,
                          size_t bytes, int width, int height, int bits_per_pixel);
void epaper_cards_clear(epaper_cards_canvas_t *canvas, card_color_t color);
void epaper_cards_pixel(epaper_cards_canvas_t *canvas, int x, int y, card_color_t color);
card_color_t epaper_cards_get(const epaper_cards_canvas_t *canvas, int x, int y);
void epaper_cards_hline(epaper_cards_canvas_t *canvas, int x, int y, int width, card_color_t color);
void epaper_cards_vline(epaper_cards_canvas_t *canvas, int x, int y, int height, card_color_t color);
void epaper_cards_rect(epaper_cards_canvas_t *canvas, int x, int y, int width, int height,
                       int radius, bool filled, card_color_t color);
// Text y is the line top; glyph bearings are relative to the font baseline.
// 文本 y 为行顶端；字形偏移量相对于字体基线。
int epaper_cards_measure(const char *text, int font_size);
void epaper_cards_text(epaper_cards_canvas_t *canvas, int x, int y, const char *text,
                       int font_size, card_color_t color);
epaper_cards_text_result_t epaper_cards_text_box(epaper_cards_canvas_t *canvas,
    int x, int y, int width, int height, const char *text, int font_size,
    card_color_t color, int max_lines);
bool epaper_cards_icon(epaper_cards_canvas_t *canvas, int x, int y,
                       const char *name, int size, card_color_t color);
bool epaper_cards_icon_scaled(epaper_cards_canvas_t *canvas, int x, int y,
                              const char *name, int source_size, int size, card_color_t color);
void epaper_cards_checkbox(epaper_cards_canvas_t *canvas, int x, int y, int size,
                           bool checked, card_color_t color);
// Inverts the six inks in pairs: black/white, yellow/blue and red/green.
// 将六种墨色成对反转：黑白、黄蓝和红绿。
void epaper_cards_invert(epaper_cards_canvas_t *canvas, int x, int y, int width, int height);

// The display task calls render; ctx stays alive through outstanding callbacks.
// 显示任务调用 render；ctx 在所有待处理回调完成前持续有效。
typedef void (*epaper_cards_render_cb)(epaper_cards_canvas_t *canvas, void *ctx);
bool epaper_cards_show(epaper_cards_render_cb render, void *ctx);
// Coalesces pending requests into a single follow-up full refresh.
// 将待处理的重绘请求合并为随后的一次全屏刷新。
bool epaper_cards_request_redraw(void);

#ifdef __cplusplus
}
#endif
