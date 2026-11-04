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

static void box(epaper_cards_canvas_t *c, int x, int y, int w, const char *text,
                 int size, card_color_t color) {
    const card_font_t *font = card_font_find(size);
    epaper_cards_text_box(c, x, y, w, font->line_height, text, size, color, 1);
}

// Prioritizes active reminders, then orders the remaining rows by local time.
// 优先排列待确认提醒，再将其余条目按本地时间排序。
static void reminder_order(const care_model_t *m, unsigned order[CARE_REMINDERS]) {
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
}

static void reminder_row(epaper_cards_canvas_t *c, const care_reminder_t *q, int y, int offset) {
    bool done = q->status == CARE_DONE;
    int label_x = done ? 134 : 184;
    int label_width = done ? 462 : q->status == CARE_MISSED ? 404 : 546;
    box(c, 20, y + 6, 102, q->time, 28, CARD_BLACK);
    if (!done) epaper_cards_icon_scaled(c, 130, y + 5, q->icon, 64, 40, CARD_BLACK);
    int size = epaper_cards_measure(q->label, 40) <= label_width ? 40 : 28;
    box(c, label_x, y + (size == 40 ? -1 : 6), label_width, q->label, size, CARD_BLACK);
    if (done) {
        char at[6]; care_model_time(q->done_at, offset, at);
        epaper_cards_checkbox(c, 616, y + 4, 42, true, CARD_BLACK);
        box(c, 674, y + 6, 110, at, 28, CARD_BLACK);
    } else {
        if (q->status == CARE_MISSED) {
            epaper_cards_rect(c, 600, y + 9, 126, 32, 6, true, CARD_BLACK);
            box(c, 600 + (126 - epaper_cards_measure("MISSED", 28)) / 2, y + 7, 126, "MISSED", 28, CARD_WHITE);
        }
        epaper_cards_checkbox(c, 742, y + 4, 42, false, CARD_BLACK);
    }
    if (q->status == CARE_DUE) epaper_cards_invert(c, 12, y, 776, 50);
    else epaper_cards_hline(c, 16, y + 49, 768, CARD_BLACK);
}

void care_model_render(epaper_cards_canvas_t *c, const care_model_t *m, int64_t now,
                        int offset, bool sensor_valid, double temp, double humidity) {
    epaper_cards_clear(c, CARD_WHITE);
    epaper_cards_rect(c, 0, 0, 800, 64, 0, true, CARD_BLACK);
    char line[160], at[6]; struct tm t; time_t local = (time_t)(now + offset * 60LL);
    if (now >= 1704067200LL && gmtime_r(&local, &t)) strftime(line, sizeof(line), "%A %d %b", &t);
    else strcpy(line, "Set the clock with Muse");
    box(c, 20, 6, 529, line, 40, CARD_WHITE);
    if (m->weather_set) {
        epaper_cards_icon(c, 560, 0, m->weather_icon, 64, CARD_WHITE);
        snprintf(line, sizeof(line), "%.0f\xC2\xB0" "C", m->weather_temp);
        box(c, 638, 6, 150, line, 40, CARD_WHITE);
    }
    snprintf(line, sizeof(line), "For %s", m->person);
    box(c, 20, 66, 385, line, 28, CARD_BLACK);
    if (sensor_valid) snprintf(line, sizeof(line), "Inside %.1f\xC2\xB0" "C  %.0f%%", temp, humidity);
    else strcpy(line, "Inside --\xC2\xB0" "C  --%");
    int inside_x = 784 - epaper_cards_measure(line, 28);
    if (inside_x < 416) inside_x = 416;
    box(c, inside_x, 66, 784 - inside_x, line, 28, CARD_BLACK);
    epaper_cards_hline(c, 16, 104, 768, CARD_BLACK);

    int active = care_model_oldest(m);
    bool two_lines = active < 0 && m->message_count && epaper_cards_measure(m->messages[0].text, 40) > 764;
    if (active >= 0) {
        const care_reminder_t *q = &m->reminders[active];
        box(c, 20, 104, 606, q->status == CARE_MISSED ? "Please confirm" : "Time for", 28, CARD_BLACK);
        int size = epaper_cards_measure(q->label, 60) <= 606 ? 60 : 40;
        box(c, 20, size == 60 ? 128 : 141, 606, q->label, size, CARD_BLACK);
        epaper_cards_icon_scaled(c, 665, 110, q->icon, 112, 96, CARD_BLACK);
    } else if (m->message_count) {
        const care_message_t *msg = &m->messages[0];
        care_model_time(msg->time, offset, at);
        snprintf(line, sizeof(line), "From %s \xE2\x80\xA2 %s", msg->from, at);
        box(c, 20, 106, 656, line, 28, CARD_BLACK);
        if (!msg->read) {
            epaper_cards_rect(c, 694, 112, 90, 32, 12, true, CARD_BLACK);
            box(c, 694 + (90 - epaper_cards_measure("NEW", 28)) / 2, 109, 90, "NEW", 28, CARD_WHITE);
        }
        epaper_cards_text_box(c, 20, 146, 764, card_font_find(40)->line_height * (two_lines ? 2 : 1),
            msg->text, 40, CARD_BLACK, two_lines ? 2 : 1);
    } else box(c, 20, 140, 764, "A message from home, always close.", 40, CARD_BLACK);

    unsigned order[CARE_REMINDERS]; reminder_order(m, order);
    unsigned visible = two_lines ? 3 : 4;
    for (unsigned i = 0; i < m->reminder_count && i < visible; i++) reminder_row(c, &m->reminders[order[i]], (two_lines ? 258 : 208) + (int)i * 50, offset);
    epaper_cards_hline(c, 16, 408, 768, CARD_BLACK);
    int footer_x = 20;
    if (m->footer_check) { epaper_cards_icon_scaled(c, 18, 415, "check_circle", 64, 28, CARD_BLACK); footer_x = 55; }
    const char *footer = m->weather_set && !strcmp(m->footer, "Ready for your family's messages") ? m->weather_text : m->footer;
    box(c, footer_x, 407, 547 - footer_x, footer, 28, CARD_BLACK);
    care_model_time(m->updated_at, offset, at);
    snprintf(line, sizeof(line), "Updated %s", at); box(c, 560, 407, 228, line, 28, CARD_BLACK);
    // The key hints use the Green, Left, Right naming order.
    // 按键提示采用 Green、Left、Right 的命名顺序。
    const char *hints[] = {"GREEN Done", "LEFT I'm OK", "RIGHT Call me"};
    for (int i = 0; i < 3; i++) {
        int x = 16 + i * 260;
        epaper_cards_rect(c, x, 447, 248, 32, 5, false, CARD_BLACK);
        box(c, x + (248 - epaper_cards_measure(hints[i], 28)) / 2, 444, 238, hints[i], 28, CARD_BLACK);
    }
}
