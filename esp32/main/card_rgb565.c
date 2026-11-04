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
#include "card_fonts.h"
#include "card_icons.h"
#include <stdlib.h>
#include <string.h>

bool card_rgb565_canvas(card_rgb565_t *c, uint16_t *pixels, size_t bytes, int w, int h) {
    if (!c || !pixels || w < 1 || h < 1 || w > 800 || h > 480
        || bytes < (size_t)w * h * 2) return false;
    *c = (card_rgb565_t){pixels, w, h};
    return true;
}

uint16_t card_rgb565_color(unsigned r, unsigned g, unsigned b) {
    return (uint16_t)(((r & 248) << 8) | ((g & 252) << 3) | ((b & 255) >> 3));
}

static void pixel(card_rgb565_t *c, int x, int y, uint16_t color) {
    if (x >= 0 && x < c->width && y >= 0 && y < c->height)
        c->pixels[(size_t)y * c->width + x] = color;
}

void card_rgb565_clear(card_rgb565_t *c, uint16_t color) {
    for (int i = 0; i < c->width * c->height; i++) c->pixels[i] = color;
}

void card_rgb565_rect(card_rgb565_t *c, int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    int64_t xe = (int64_t)x + w, ye = (int64_t)y + h;
    if (xe > c->width) xe = c->width;
    if (ye > c->height) ye = c->height;
    for (int py = y < 0 ? 0 : y; py < ye; py++)
        for (int px = x < 0 ? 0 : x; px < xe; px++) pixel(c, px, py, color);
}

void card_rgb565_line(card_rgb565_t *c, int x, int y, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x), sx = x < x1 ? 1 : -1;
    int dy = -abs(y1 - y), sy = y < y1 ? 1 : -1, error = dx + dy;
    for (;;) {
        pixel(c, x, y, color);
        if (x == x1 && y == y1) break;
        int twice = error * 2;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}

void card_rgb565_ring(card_rgb565_t *c, int x, int y, int radius, int thickness, uint16_t color) {
    int inner = radius - thickness;
    if (inner < 0) inner = 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int d = dx * dx + dy * dy;
            if (d <= radius * radius && d >= inner * inner) pixel(c, x + dx, y + dy, color);
        }
    }
}

// Decodes supported UTF-8 punctuation and substitutes missing glyphs.
// 解码支持的 UTF-8 标点，并替换缺失的字形。
static uint32_t next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp = *s++;
    if (cp < 128) { *p = (const char *)s; return cp; }
    int count = cp >= 0xc2 && cp < 0xe0 ? 1 : cp >= 0xe0 && cp < 0xf0 ? 2 : 0;
    if (!count) { *p = (const char *)s; return '?'; }
    cp &= (1u << (6 - count)) - 1;
    for (int i = 0; i < count; i++) {
        if (!*s || (*s & 0xc0) != 0x80) { *p = (const char *)s; return '?'; }
        cp = (cp << 6) | (*s++ & 63);
    }
    *p = (const char *)s;
    return cp;
}

int card_rgb565_measure(const char *text, int size) {
    const card_font_t *f = card_font_find(size);
    int width = 0;
    if (!f || !text) return 0;
    while (*text) width += card_font_glyph(f, next(&text))->advance;
    return width;
}

static void mask(card_rgb565_t *c, int x, int y, int w, int h,
                 const uint8_t *data, uint16_t color) {
    int stride = (w + 7) / 8;
    for (int py = 0; py < h; py++)
        for (int px = 0; px < w; px++)
            if (data[py * stride + px / 8] & (0x80 >> (px % 8))) pixel(c, x + px, y + py, color);
}

void card_rgb565_text(card_rgb565_t *c, int x, int y, int width, const char *text,
                     int size, uint16_t color) {
    const card_font_t *f = card_font_find(size);
    if (!f || !text || width <= 0) return;
    int used = 0;
    bool clipped = card_rgb565_measure(text, size) > width;
    const card_glyph_t *ellipsis = card_font_glyph(f, 0x2026);
    while (*text) {
        const card_glyph_t *g = card_font_glyph(f, next(&text));
        if (used + g->advance + (clipped ? ellipsis->advance : 0) > width) {
            if (used + ellipsis->advance <= width)
                mask(c, x + used + ellipsis->x_offset, y + f->ascent + ellipsis->y_offset,
                     ellipsis->width, ellipsis->height, f->bitmap + ellipsis->offset, color);
            break;
        }
        mask(c, x + used + g->x_offset, y + f->ascent + g->y_offset,
             g->width, g->height, f->bitmap + g->offset, color);
        used += g->advance;
    }
}

bool card_rgb565_icon(card_rgb565_t *c, int x, int y, const char *name, int size, uint16_t color) {
    const card_icon_t *i = card_icon_find(name, size);
    if (!i) return false;
    mask(c, x + i->x_offset, y + i->y_offset, i->width, i->height, i->bitmap, color);
    return true;
}
