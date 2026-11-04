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

#include "air_guardian_core.h"
#include <stdio.h>
#include <stdlib.h>

static uint16_t rgb(unsigned r, unsigned g, unsigned b) { return card_rgb565_color(r, g, b); }
static uint16_t level_color(int level) {
    static const unsigned colors[][3] = {{81, 210, 159}, {232, 206, 95}, {245, 157, 76}, {245, 101, 93}};
    if (level < 0) return rgb(120, 134, 145);
    return rgb(colors[level][0], colors[level][1], colors[level][2]);
}

static void center(card_rgb565_t *c, int y, const char *text, int size, uint16_t color) {
    if (size > 28 && card_rgb565_measure(text, size) > 432) size = 28;
    int width = card_rgb565_measure(text, size);
    if (width > 432) width = 432;
    card_rgb565_text(c, (480 - width) / 2, y, width, text, size, color);
}

static void value_text(const air_core_t *c, int metric, char *out, size_t cap) {
    if (!(c->valid & (1u << metric))) snprintf(out, cap, "--");
    else snprintf(out, cap, metric < 2 ? "%.0f" : "%.1f", (double)c->current[metric]);
}

// Draws the 24-hour CO2 averages with gaps between missing buckets.
// 绘制最近 24 小时的 CO2 平均值，并在缺失区间断开曲线。
static void sparkline(card_rgb565_t *canvas, const air_core_t *c, int64_t now_s, uint16_t color) {
    air_buckets_t *b = malloc(sizeof(*b));
    if (!b) return;
    air_core_buckets(c, 24, 60, now_s, b);
    float low = 400, high = c->settings.limits[0];
    bool any = false;
    for (unsigned n = 0; n < b->count; n++) {
        air_bucket_t *v = &b->metrics[0][n];
        if (!v->count) continue;
        any = true;
        if (v->avg < low) low = v->avg;
        if (v->avg > high) high = v->avg;
    }
    if (high - low < 100) high = low + 100;
    card_rgb565_line(canvas, 28, 414, 452, 414, rgb(50, 64, 72));
    bool previous = false;
    int px = 0, py = 0;
    for (unsigned n = 0; n < b->count; n++) {
        air_bucket_t *v = &b->metrics[0][n];
        if (!v->count) { previous = false; continue; }
        int x = 30 + (int)n * 420 / (int)(b->count - 1);
        int y = 410 - (int)((v->avg - low) * 38 / (high - low));
        if (previous) card_rgb565_line(canvas, px, py, x, y, color);
        card_rgb565_rect(canvas, x - 2, y - 2, 4, 4, color);
        previous = true; px = x; py = y;
    }
    if (!any) card_rgb565_text(canvas, 28, 376, 424, "Collecting history", 28, rgb(120, 134, 145));
    free(b);
}

void air_guardian_render(card_rgb565_t *canvas, const air_core_t *c, int64_t now_s,
                         int minutes, bool pulse) {
    air_view_t view = air_effective_view(c->settings.view, minutes);
    bool night = view == AIR_NIGHT;
    uint16_t text = night ? rgb(145, 109, 73) : rgb(231, 239, 235);
    uint16_t muted = night ? rgb(103, 78, 54) : rgb(143, 166, 162);
    card_rgb565_clear(canvas, night ? rgb(3, 3, 4) : rgb(13, 25, 30));
    int worst = -1;
    bool alert = false;
    for (int i = 0; i < AIR_METRICS; i++) {
        int level = air_core_level(c, i);
        if (level > worst) worst = level;
    }
    for (int i = 0; i < AIR_LIMITS; i++) alert |= c->alarms[i].active;
    uint16_t color = night ? rgb(133, 90, 51) : level_color(worst);
    center(canvas, 22, c->settings.room, 28, muted);
    char value[32], line[64];
    if (view == AIR_NUMBERS) {
        static const char *const labels[] = {"CO2", "tVOC", "Temp", "Humidity"};
        static const char *const icons[] = {"co2", "air", "thermostat", "humidity_percentage"};
        static const char *const suffixes[] = {"ppm", "index", "\302\260C", "% RH"};
        for (int i = 0; i < AIR_METRICS; i++) {
            int x = 24 + (i % 2) * 228, y = 70 + (i / 2) * 135;
            card_rgb565_rect(canvas, x, y, 204, 125, rgb(23, 40, 46));
            card_rgb565_text(canvas, x + 12, y + 6, 180, labels[i], 28, muted);
            value_text(c, i, value, sizeof(value));
            card_rgb565_text(canvas, x + 12, y + 39, 180, value, 40, level_color(air_core_level(c, i)));
            card_rgb565_text(canvas, x + 12, y + 87, 112, suffixes[i], 28, muted);
            card_rgb565_icon(canvas, x + 140, y + 62, icons[i], 64, muted);
        }
        card_rgb565_text(canvas, 28, 340, 424, "CO2 / last 24 hours", 28, muted);
        sparkline(canvas, c, now_s, color);
    } else if (night) {
        card_rgb565_icon(canvas, 208, 96, "bedtime", 64, muted);
        value_text(c, 0, value, sizeof(value));
        center(canvas, 180, value, 96, text);
        center(canvas, 290, "CO2 ppm", 28, muted);
        center(canvas, 345, air_core_mood(c), 28, text);
    } else {
        card_rgb565_ring(canvas, 240, 207, 130, 13, color);
        card_rgb565_icon(canvas, 184, 150, "potted_plant", 112, color);
        center(canvas, 355, air_core_mood(c), 40, text);
        value_text(c, 0, value, sizeof(value));
        snprintf(line, sizeof(line), "CO2 %s ppm", value);
        center(canvas, 407, line, 28, muted);
    }
    if (alert) {
        uint16_t border = night ? (pulse ? rgb(151, 74, 47) : rgb(92, 47, 29))
            : (pulse ? rgb(245, 101, 93) : rgb(130, 56, 53));
        card_rgb565_rect(canvas, 0, 0, 480, 6, border);
        card_rgb565_rect(canvas, 0, 474, 480, 6, border);
        card_rgb565_rect(canvas, 0, 0, 6, 480, border);
        card_rgb565_rect(canvas, 474, 0, 6, 480, border);
        card_rgb565_rect(canvas, 12, 437, 456, 33, night ? rgb(3, 3, 4) : rgb(13, 25, 30));
        center(canvas, 438, air_core_advice(c), 28, text);
    } else if (view == AIR_NUMBERS) center(canvas, 436, air_core_mood(c), 28, text);
}
