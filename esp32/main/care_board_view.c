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

#include "care_model.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Samples a 64-pixel source icon into a 28-pixel reminder-row mask.
// 将 64 像素源图标采样成提醒行使用的 28 像素图案。
static void small_icon(epaper_cards_canvas_t *c, int x, int y, const char *name) {
    const card_icon_t *icon = card_icon_find(name, 64);
    if (!icon) return;
    int stride = (icon->width + 7) / 8;
    for (int row = 0; row < 28; row++) for (int col = 0; col < 28; col++) {
        int sx = col * 64 / 28 - icon->x_offset, sy = row * 64 / 28 - icon->y_offset;
        if (sx >= 0 && sy >= 0 && sx < icon->width && sy < icon->height
            && (icon->bitmap[sy * stride + sx / 8] & (0x80 >> (sx % 8)))) epaper_cards_pixel(c, x + col, y + row, CARD_BLACK);
    }
}

static void box(epaper_cards_canvas_t *c, int x, int y, int w, int h, const char *s, int size, int lines) {
    const card_font_t *font = card_font_find(size);
    if (font && h < font->line_height * lines) h = font->line_height * lines;
    epaper_cards_text_box(c, x, y, w, h, s, size, CARD_BLACK, lines);
}

void care_model_render(epaper_cards_canvas_t *c, const care_model_t *m, int64_t now,
                        int offset, bool sensor_valid, double temp, double humidity) {
    epaper_cards_clear(c, CARD_WHITE);
    char line[160], at[6]; struct tm t; time_t local = (time_t)(now + offset * 60LL);
    if (now >= 1704067200LL && gmtime_r(&local, &t)) strftime(line, sizeof(line), "%a %d %b", &t);
    else strcpy(line, "Set the clock with Muse");
    box(c, 18, 4, 520, 49, line, 40, 1);
    snprintf(line, sizeof(line), "%s - %s", m->person, m->title[0] ? m->title : "Care Board");
    box(c, 18, 56, 395, 35, line, 28, 1);
    if (m->weather_set) {
        epaper_cards_icon(c, 554, 0, m->weather_icon, 64, CARD_BLACK);
        snprintf(line, sizeof(line), "%.0f\xC2\xB0" "C", m->weather_temp);
        box(c, 637, 5, 152, 49, line, 40, 1);
        box(c, 18, 94, 764, 35, m->weather_text, 28, 1);
    }
    if (sensor_valid) snprintf(line, sizeof(line), "Inside %.1f\xC2\xB0" "C  %.0f%%", temp, humidity);
    else strcpy(line, "Inside --\xC2\xB0" "C  --%");
    box(c, 425, 61, 362, 35, line, 28, 1);
    epaper_cards_hline(c, 16, 130, 768, CARD_BLACK);
    int active = care_model_oldest(m);
    if (active >= 0) {
        const care_reminder_t *q = &m->reminders[active];
        box(c, 20, 132, 600, 35, q->status == CARE_MISSED ? "Please confirm" : "Time for", 28, 1);
        int size = epaper_cards_measure(q->label, 96) <= 606 ? 96
            : epaper_cards_measure(q->label, 60) <= 606 ? 60 : 40;
        box(c, 20, size == 96 ? 148 : 168, 606, size == 96 ? 119 : 99, q->label, size, size == 40 ? 2 : 1);
        epaper_cards_icon(c, 655, 143, q->icon, 112, CARD_BLACK);
    } else if (m->message_count) {
        const care_message_t *msg = &m->messages[0];
        care_model_time(msg->time, offset, at);
        snprintf(line, sizeof(line), "From %s \xE2\x80\xA2 %s", msg->from, at);
        box(c, 20, 136, 656, 35, line, 28, 1);
        if (!msg->read) { epaper_cards_rect(c, 694, 138, 90, 32, 3, false, CARD_BLACK); box(c, 704, 136, 75, 35, "NEW", 28, 1); }
        int size = epaper_cards_measure(msg->text, 60) <= 756 ? 60 : 40;
        box(c, 20, 174, 756, 99, msg->text, size, size == 60 ? 1 : 2);
    } else {
        box(c, 22, 156, 730, 99, "Your family's messages will appear here.", 40, 2);
    }
    // Prioritizes active reminders, then the remaining entries in time order.
    // 优先排列待确认提醒，再将其他条目按时间排列。
    unsigned order[CARE_REMINDERS];
    for (unsigned i = 0; i < m->reminder_count; i++) order[i] = i;
    for (unsigned i = 1; i < m->reminder_count; i++) {
        unsigned index = order[i], j = i;
        int rank = m->reminders[index].status == CARE_DUE || m->reminders[index].status == CARE_MISSED ? 0 : 1;
        while (j) {
            const care_reminder_t *q = &m->reminders[order[j - 1]];
            int before = q->status == CARE_DUE || q->status == CARE_MISSED ? 0 : 1;
            if (before < rank || (before == rank && q->minutes <= m->reminders[index].minutes)) break;
            order[j] = order[j - 1]; j--;
        }
        order[j] = index;
    }
    for (unsigned i = 0; i < m->reminder_count && i < 4; i++) {
        const care_reminder_t *q = &m->reminders[order[i]]; int y = 273 + (int)i * 32;
        box(c, 18, y - 4, 99, 35, q->time, 28, 1); small_icon(c, 122, y, q->icon);
        box(c, 163, y - 4, q->status == CARE_MISSED ? 440 : 566, 35, q->label, 28, 1);
        if (q->status == CARE_MISSED) box(c, 611, y - 4, 130, 35, "MISSED", 28, 1);
        epaper_cards_checkbox(c, 754, y + 2, 26, q->status == CARE_DONE, CARD_BLACK);
        if (q->status == CARE_DUE) epaper_cards_invert(c, 12, y - 1, 776, 32);
    }
    epaper_cards_hline(c, 16, 402, 768, CARD_BLACK);
    int x = 18;
    if (m->footer_check) { small_icon(c, 18, 408, "check_circle"); x = 55; }
    box(c, x, 404, 557 - x, 35, m->footer, 28, 1);
    care_model_time(m->updated_at, offset, at);
    snprintf(line, sizeof(line), "Updated %s", at); box(c, 560, 404, 227, 35, line, 28, 1);
    box(c, 18, 441, 770, 35, "GREEN Done \xE2\x80\xA2 LEFT I'm OK \xE2\x80\xA2 RIGHT Call me", 28, 1);
}
