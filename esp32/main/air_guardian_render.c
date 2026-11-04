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

static uint16_t rgb(unsigned r, unsigned g, unsigned b) { return card_rgb565_color(r, g, b); }

static uint16_t category_color(air_category_t category, bool night) {
    static const unsigned colors[AIR_CATEGORY_COUNT][3] = {
        {81, 210, 159}, {232, 206, 95}, {245, 157, 76},
        {245, 101, 93}, {185, 128, 210}, {177, 76, 111}
    };
    unsigned divisor = night ? 4 : 1;
    return rgb(colors[category][0] / divisor, colors[category][1] / divisor, colors[category][2] / divisor);
}

static void center(card_rgb565_t *c, int y, const char *text, int size, uint16_t color) {
    if (size > 28 && card_rgb565_measure(text, size) > 432) size = size > 60 ? 60 : 28;
    int width = card_rgb565_measure(text, size);
    if (width > 432) width = 432;
    card_rgb565_text(c, (480 - width) / 2, y, width, text, size, color);
}

static void metric(char *out, size_t cap, double value, bool integer) {
    if (value < 0) snprintf(out, cap, "--");
    else snprintf(out, cap, integer ? "%.0f" : "%.1f", value);
}

// Formats the observation time in the local clock, or explicitly in UTC before sync.
// 按本地时钟格式化观测时间，同步前明确标注为 UTC。
static void updated_text(char *out, size_t cap, const air_core_t *c, int64_t epoch, int minutes) {
    if (!c->snapshot.present) { snprintf(out, cap, "No snapshot saved"); return; }
    int observed = (int)(c->snapshot.updated_utc / 60 % 1440);
    bool local = epoch >= AIR_MIN_EPOCH && epoch <= AIR_MAX_EPOCH && minutes >= 0 && minutes < 1440;
    if (local) observed = (observed + minutes - (int)(epoch / 60 % 1440) + 1440) % 1440;
    snprintf(out, cap, "Updated %02d:%02d%s", observed / 60, observed % 60, local ? "" : " UTC");
}

void air_guardian_render(card_rgb565_t *canvas, const air_core_t *c,
                         int64_t epoch, int local_minutes) {
    const air_snapshot_t *s = &c->snapshot;
    bool night = c->view == AIR_NIGHT;
    uint16_t bg = night ? rgb(3, 3, 4) : rgb(13, 25, 30);
    uint16_t text = night ? rgb(145, 109, 73) : rgb(231, 239, 235);
    uint16_t muted = night ? rgb(103, 78, 54) : rgb(143, 166, 162);
    uint16_t color = s->present ? category_color(s->category, night) : muted;
    int64_t age = air_snapshot_age(c, epoch);
    const char *title = s->present && age < 0 ? "Outdoor Air - age unknown"
        : age >= AIR_STALE_S ? "Outdoor Air - refresh needed" : "Outdoor Air";
    card_rgb565_clear(canvas, bg);
    card_rgb565_rect(canvas, 0, 0, 480, 10, color);
    center(canvas, 28, title, 28, muted);
    center(canvas, 70, s->place[0] ? s->place : "Your place", 28, text);
    center(canvas, 120, s->present ? air_category_labels[s->category] : "Waiting for Muse", 40, color);
    char value[32], line[80];
    if (c->view == AIR_NUMBERS) {
        for (int i = 0; i < 2; i++) {
            int x = 24 + i * 228;
            card_rgb565_rect(canvas, x, 190, 204, 140, rgb(23, 40, 46));
            card_rgb565_text(canvas, x + 12, 201, 180, i == 0 ? "US AQI" : "PM2.5", 28, muted);
            metric(value, sizeof(value), i == 0 ? s->aqi : s->pm25, i == 0);
            int size = card_rgb565_measure(value, 60) > 180 ? 40 : 60;
            card_rgb565_text(canvas, x + 12, 239, 180, value, size, text);
        }
        center(canvas, 334, "PM2.5: ug/m3", 28, muted);
    } else {
        bool aqi = s->aqi >= 0;
        metric(value, sizeof(value), aqi ? s->aqi : s->pm25, aqi);
        center(canvas, 190, value, 96, text);
        center(canvas, 303, aqi ? "US AQI" : "PM2.5 ug/m3", 28, muted);
    }
    center(canvas, 376, s->present ? s->advice : "Ask Muse for outdoor air.", 28, text);
    card_rgb565_rect(canvas, 24, 417, 432, 1, muted);
    updated_text(line, sizeof(line), c, epoch, local_minutes);
    center(canvas, 435, line, 28, muted);
}
