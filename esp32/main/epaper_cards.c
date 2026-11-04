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

#include "epaper_cards.h"
#include "card_mask.h"

#include <limits.h>
#include <string.h>

bool epaper_cards_canvas(epaper_cards_canvas_t *c, uint8_t *pixels, size_t bytes,
                          int width, int height, int bpp) {
    if (!c || !pixels || width <= 0 || height <= 0 || width > 800 || height > 480
        || (bpp != 1 && bpp != 4)) return false;
    int stride = (width * bpp + 7) / 8;
    if (bytes < (size_t)stride * height) return false;
    *c = (epaper_cards_canvas_t){pixels, width, height, bpp, stride};
    return true;
}

static uint8_t ink(const epaper_cards_canvas_t *c, card_color_t color) {
    if (c->bits_per_pixel == 1) return color == CARD_WHITE;
    return color == CARD_WHITE || color == CARD_YELLOW || color == CARD_RED
        || color == CARD_BLUE || color == CARD_GREEN ? (uint8_t)color : CARD_BLACK;
}

void epaper_cards_clear(epaper_cards_canvas_t *c, card_color_t color) {
    uint8_t v = ink(c, color);
    memset(c->pixels, c->bits_per_pixel == 1 ? (v ? 255 : 0) : v * 17,
           (size_t)c->stride * c->height);
}

void epaper_cards_pixel(epaper_cards_canvas_t *c, int x, int y, card_color_t color) {
    if (x < 0 || y < 0 || x >= c->width || y >= c->height) return;
    int shift = c->bits_per_pixel == 1 ? 7 - x % 8 : (x & 1 ? 0 : 4);
    uint8_t mask = (c->bits_per_pixel == 1 ? 1 : 15) << shift;
    uint8_t *p = c->pixels + (size_t)y * c->stride + x * c->bits_per_pixel / 8;
    *p = (*p & ~mask) | (ink(c, color) << shift);
}

card_color_t epaper_cards_get(const epaper_cards_canvas_t *c, int x, int y) {
    if (x < 0 || y < 0 || x >= c->width || y >= c->height) return CARD_WHITE;
    int shift = c->bits_per_pixel == 1 ? 7 - x % 8 : (x & 1 ? 0 : 4);
    uint8_t p = c->pixels[(size_t)y * c->stride + x * c->bits_per_pixel / 8];
    return (card_color_t)((p >> shift) & (c->bits_per_pixel == 1 ? 1 : 15));
}

static int clipped_end(int start, int length, int limit) {
    int64_t end = (int64_t)start + length;
    return end < 0 ? 0 : end > limit ? limit : (int)end;
}

void epaper_cards_hline(epaper_cards_canvas_t *c, int x, int y, int width, card_color_t color) {
    if (width <= 0 || y < 0 || y >= c->height) return;
    int end = clipped_end(x, width, c->width);
    for (int i = x < 0 ? 0 : x; i < end; i++) epaper_cards_pixel(c, i, y, color);
}

void epaper_cards_vline(epaper_cards_canvas_t *c, int x, int y, int height, card_color_t color) {
    if (height <= 0 || x < 0 || x >= c->width) return;
    int end = clipped_end(y, height, c->height);
    for (int i = y < 0 ? 0 : y; i < end; i++) epaper_cards_pixel(c, x, i, color);
}

// Tests a point against the straight edges and circular corners of a rectangle.
// 检查一个点是否位于矩形直边和圆弧角围成的区域内。
static bool in_round_rect(int64_t x, int64_t y, int w, int h, int r) {
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    if (r <= 0) return true;
    int64_t dx = x < r ? r - 1 - x : x >= w - r ? x - (w - r) : 0;
    int64_t dy = y < r ? r - 1 - y : y >= h - r ? y - (h - r) : 0;
    return dx * dx + dy * dy < (int64_t)r * r;
}

void epaper_cards_rect(epaper_cards_canvas_t *c, int x, int y, int w, int h,
                       int radius, bool filled, card_color_t color) {
    if (w <= 0 || h <= 0) return;
    int r = radius < 0 ? 0 : radius;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int xe = clipped_end(x, w, c->width), ye = clipped_end(y, h, c->height);
    for (int py = y < 0 ? 0 : y; py < ye; py++) {
        for (int px = x < 0 ? 0 : x; px < xe; px++) {
            int64_t rx = (int64_t)px - x, ry = (int64_t)py - y;
            if (in_round_rect(rx, ry, w, h, r) && (filled
                || !in_round_rect(rx - 1, ry - 1, w - 2, h - 2, r > 0 ? r - 1 : 0))) {
                epaper_cards_pixel(c, px, py, color);
            }
        }
    }
}

// Decodes one UTF-8 character, substituting '?' for malformed sequences.
// 解码一个 UTF-8 字符，将无效序列替换为问号。
static uint32_t next_char(const char **text, const char *end) {
    const unsigned char *p = (const unsigned char *)*text;
    if ((const char *)p >= end) return 0;
    uint32_t cp = *p++;
    if (cp < 128) { *text = (const char *)p; return cp; }
    int n = cp >= 0xC2 && cp <= 0xDF ? 1 : cp >= 0xE0 && cp <= 0xEF ? 2
          : cp >= 0xF0 && cp <= 0xF4 ? 3 : 0;
    if (!n) { *text = (const char *)p; return '?'; }
    uint32_t v = cp & ((1u << (6 - n)) - 1);
    for (int i = 0; i < n; i++) {
        if ((const char *)p >= end || (*p & 0xC0) != 0x80) {
            *text = (const char *)p; return '?';
        }
        v = (v << 6) | (*p++ & 63);
    }
    *text = (const char *)p;
    return v < (n == 1 ? 0x80u : n == 2 ? 0x800u : 0x10000u)
        || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF) ? '?' : v;
}

static int span_width(const card_font_t *font, const char *p, const char *end) {
    int64_t width = 0;
    while (p < end) width += card_font_glyph(font, next_char(&p, end))->advance;
    return width > INT_MAX ? INT_MAX : (int)width;
}

int epaper_cards_measure(const char *text, int size) {
    const card_font_t *font = card_font_find(size);
    if (!font || !text) return 0;
    int width = 0;
    const char *p = text;
    for (;;) {
        const char *end = strchr(p, '\n');
        int w = span_width(font, p, end ? end : p + strlen(p));
        if (w > width) width = w;
        if (!end) return width;
        p = end + 1;
    }
}

static void mask_draw(epaper_cards_canvas_t *c, int64_t x, int64_t y, int width, int height,
                       const uint8_t *bits, uint16_t encoded_bytes, card_color_t color) {
    card_mask_decoder_t decoder;
    if (!card_mask_begin(&decoder, bits, encoded_bytes, width, height)) return;
    for (int row = 0; row < height; row++) {
        const uint8_t *pixels = card_mask_next(&decoder);
        if (!pixels) return;
        if (y + row < 0 || y + row >= c->height) continue;
        for (int col = 0; col < width; col++) {
            if (x + col < 0 || x + col >= c->width) continue;
            if (pixels[col / 8] & (0x80 >> (col % 8))) {
                epaper_cards_pixel(c, (int)(x + col), (int)(y + row), color);
            }
        }
    }
}

static void draw_span(epaper_cards_canvas_t *c, int64_t x, int64_t y, const char *p,
                       const char *end, const card_font_t *font, card_color_t color) {
    while (p < end && x < c->width) {
        const card_glyph_t *g = card_font_glyph(font, next_char(&p, end));
        mask_draw(c, x + g->x_offset, y + font->ascent + g->y_offset,
                  g->width, g->height, font->bitmap + g->offset, g->encoded_bytes, color);
        x += g->advance;
    }
}

void epaper_cards_text(epaper_cards_canvas_t *c, int x, int y, const char *text,
                       int size, card_color_t color) {
    const card_font_t *font = card_font_find(size);
    if (!font || !text) return;
    const char *p = text;
    int64_t top = y;
    for (;;) {
        const char *end = strchr(p, '\n');
        draw_span(c, x, top, p, end ? end : p + strlen(p), font, color);
        if (!end) return;
        p = end + 1;
        top += font->line_height;
    }
}

// Fits complete words, splitting an oversized word at a character boundary.
// 优先按完整单词排版，过长单词在字符边界处拆行。
epaper_cards_text_result_t epaper_cards_text_box(epaper_cards_canvas_t *c,
    int x, int y, int width, int height, const char *text, int size,
    card_color_t color, int max_lines) {
    epaper_cards_text_result_t result = {0};
    const card_font_t *font = card_font_find(size);
    if (!font || !text || !*text || width <= 0 || height <= 0 || max_lines <= 0) return result;
    int fit = height / font->line_height;
    if (max_lines > fit) max_lines = fit;
    const char *p = text, *end = text + strlen(text);
    if (max_lines == 0) { result.truncated = true; return result; }
    while (p < end && result.lines < max_lines) {
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p == end) break;
        const char *start = p, *stop = p, *space = NULL, *scan = p;
        int used = 0;
        while (scan < end && *scan != '\n') {
            const char *before = scan;
            uint32_t cp = next_char(&scan, end);
            int advance = card_font_glyph(font, cp)->advance;
            if ((int64_t)used + advance > width) {
                if (cp == ' ' || cp == '\t') { stop = before; }
                else if (space) { stop = space; scan = space + 1; }
                else { stop = before; scan = before == start ? scan : before; }
                break;
            }
            if (cp == ' ' || cp == '\t') space = before;
            used += advance;
            stop = scan;
        }
        if (scan < end && *scan == '\n') scan++;
        while (stop > start && (stop[-1] == ' ' || stop[-1] == '\t')) stop--;
        const char *remaining = scan;
        while (remaining < end && (*remaining == ' ' || *remaining == '\t')) remaining++;
        bool more = remaining < end;
        bool ellipsis = result.lines + 1 == max_lines && more;
        int ell_width = card_font_glyph(font, 0x2026)->advance;
        if (ellipsis) {
            while (stop > start && (int64_t)span_width(font, start, stop) + ell_width > width) {
                stop--;
                while (stop > start && ((unsigned char)*stop & 0xC0) == 0x80) stop--;
            }
        }
        int64_t top = (int64_t)y + (int64_t)result.lines * font->line_height;
        draw_span(c, x, top, start, stop, font, color);
        if (ellipsis && ell_width <= width) {
            static const char dots[] = "\xE2\x80\xA6";
            draw_span(c, (int64_t)x + span_width(font, start, stop), top, dots, dots + 3, font, color);
        }
        result.lines++;
        result.truncated |= ellipsis || (stop == start && scan > start && start[0] != '\n');
        p = scan;
    }
    return result;
}

bool epaper_cards_icon(epaper_cards_canvas_t *c, int x, int y, const char *name,
                       int size, card_color_t color) {
    const card_icon_t *icon = card_icon_find(name, size);
    if (!icon) return false;
    mask_draw(c, (int64_t)x + icon->x_offset, (int64_t)y + icon->y_offset,
              icon->width, icon->height, icon->bitmap, icon->encoded_bytes, color);
    return true;
}

// Resamples a decoded icon row by row without a full-size temporary bitmap.
// 逐行解码并缩放图标，使用行缓冲区保存临时图案。
bool epaper_cards_icon_scaled(epaper_cards_canvas_t *c, int x, int y, const char *name,
                              int source_size, int size, card_color_t color) {
    const card_icon_t *icon = card_icon_find(name, source_size);
    card_mask_decoder_t decoder;
    if (!icon || size <= 0 || size > 800 || !card_mask_begin(&decoder, icon->bitmap,
        icon->encoded_bytes, icon->width, icon->height)) return false;
    int decoded = -1;
    const uint8_t *row = NULL;
    for (int dy = 0; dy < size; dy++) {
        int sy = dy * source_size / size - icon->y_offset;
        if (sy < 0 || sy >= icon->height) continue;
        while (decoded < sy) { row = card_mask_next(&decoder); decoded++; if (!row) return false; }
        for (int dx = 0; dx < size; dx++) {
            int sx = dx * source_size / size - icon->x_offset;
            int64_t px = (int64_t)x + dx, py = (int64_t)y + dy;
            if (sx >= 0 && sx < icon->width && px >= 0 && px < c->width && py >= 0 && py < c->height
                && (row[sx / 8] & (0x80 >> (sx % 8)))) epaper_cards_pixel(c, (int)px, (int)py, color);
        }
    }
    return true;
}

void epaper_cards_checkbox(epaper_cards_canvas_t *c, int x, int y, int size,
                           bool checked, card_color_t color) {
    if (size <= 0 || size > 480 || x >= c->width || y >= c->height
        || (int64_t)x + size <= 0 || (int64_t)y + size <= 0) return;
    epaper_cards_rect(c, x, y, size, size, size / 8, false, color);
    if (!checked) return;
    for (int i = size / 5; i < size * 4 / 5; i++) {
        int row = i < size * 2 / 5 ? size / 2 + i - size / 5 : size * 11 / 10 - i;
        epaper_cards_rect(c, x + i, y + row, 3, 3, 0, true, color);
    }
}

void epaper_cards_invert(epaper_cards_canvas_t *c, int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return;
    static const card_color_t inverse[] = {CARD_WHITE, CARD_BLACK, CARD_BLUE,
                                          CARD_GREEN, CARD_BLACK, CARD_YELLOW, CARD_RED};
    int xe = clipped_end(x, width, c->width), ye = clipped_end(y, height, c->height);
    for (int py = y < 0 ? 0 : y; py < ye; py++) {
        for (int px = x < 0 ? 0 : x; px < xe; px++) {
            unsigned color = epaper_cards_get(c, px, py);
            epaper_cards_pixel(c, px, py, color < 7 ? inverse[color] : CARD_BLACK);
        }
    }
}
