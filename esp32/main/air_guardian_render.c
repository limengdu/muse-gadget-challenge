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
#include <string.h>

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

// Projects only visible text, colours and view into a zero-initialized key.
// 将可见文字、颜色和视图投影为已清零的画面比较记录。
void air_guardian_screen(air_screen_t *out, const air_core_t *c, int64_t epoch, int local_minutes) {
    const air_snapshot_t *s = &c->snapshot;
    memset(out, 0, sizeof(*out));
    out->view = c->view;
    out->category = s->present ? s->category : -1;
    int64_t age = air_snapshot_age(c, epoch);
    out->age_label = s->present && age < 0 ? -1 : age >= AIR_STALE_S ? 1 : 0;
    snprintf(out->place, sizeof(out->place), "%s", s->place[0] ? s->place : "Your place");
    snprintf(out->advice, sizeof(out->advice), "%s", s->present ? s->advice : "Ask Muse for outdoor air.");
    out->aqi = c->view == AIR_NUMBERS || s->aqi >= 0;
    metric(out->values[0], sizeof(out->values[0]), out->aqi ? s->aqi : s->pm25, out->aqi);
    if (c->view == AIR_NUMBERS) metric(out->values[1], sizeof(out->values[1]), s->pm25, false);
    updated_text(out->updated, sizeof(out->updated), c, epoch, local_minutes);
}

int64_t air_guardian_next_change(const air_core_t *c, int64_t epoch) {
    if (!c->snapshot.present || epoch < AIR_MIN_EPOCH || epoch > AIR_MAX_EPOCH) return -1;
    if (epoch < c->snapshot.updated_utc) return c->snapshot.updated_utc - epoch;
    int64_t remaining = c->snapshot.updated_utc + AIR_STALE_S - epoch;
    return remaining > 0 ? remaining : -1;
}

void air_guardian_render_screen(card_rgb565_t *canvas, const air_screen_t *screen) {
    bool night = screen->view == AIR_NIGHT;
    uint16_t bg = night ? rgb(3, 3, 4) : rgb(13, 25, 30);
    uint16_t text = night ? rgb(145, 109, 73) : rgb(231, 239, 235);
    uint16_t muted = night ? rgb(103, 78, 54) : rgb(143, 166, 162);
    uint16_t color = screen->category >= 0 ? category_color(screen->category, night) : muted;
    const char *title = screen->age_label < 0 ? "Outdoor Air - age unknown"
        : screen->age_label > 0 ? "Outdoor Air - refresh needed" : "Outdoor Air";
    card_rgb565_clear(canvas, bg);
    card_rgb565_rect(canvas, 0, 0, 480, 10, color);
    center(canvas, 28, title, 28, muted);
    center(canvas, 70, screen->place, 28, text);
    center(canvas, 120, screen->category >= 0 ? air_category_labels[screen->category] : "Waiting for Muse", 40, color);
    if (screen->view == AIR_NUMBERS) {
        for (int i = 0; i < 2; i++) {
            int x = 24 + i * 228;
            card_rgb565_rect(canvas, x, 190, 204, 140, rgb(23, 40, 46));
            card_rgb565_text(canvas, x + 12, 201, 180, i == 0 ? "US AQI" : "PM2.5", 28, muted);
            const char *value = screen->values[i];
            int size = card_rgb565_measure(value, 60) > 180 ? 40 : 60;
            card_rgb565_text(canvas, x + 12, 239, 180, value, size, text);
        }
        center(canvas, 334, "PM2.5: ug/m3", 28, muted);
    } else {
        center(canvas, 190, screen->values[0], 96, text);
        center(canvas, 303, screen->aqi ? "US AQI" : "PM2.5 ug/m3", 28, muted);
    }
    center(canvas, 376, screen->advice, 28, text);
    card_rgb565_rect(canvas, 24, 417, 432, 1, muted);
    center(canvas, 435, screen->updated, 28, muted);
}

void air_guardian_render(card_rgb565_t *canvas, const air_core_t *c,
                         int64_t epoch, int local_minutes) {
    air_screen_t screen;
    air_guardian_screen(&screen, c, epoch, local_minutes);
    air_guardian_render_screen(canvas, &screen);
}
