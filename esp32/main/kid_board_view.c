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

#include "kid_model.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static void box(epaper_cards_canvas_t *c, int x, int y, int width, const char *s, int size, card_color_t ink) {
    epaper_cards_text_box(c, x, y, width, card_font_find(size)->line_height, s, size, ink, 1);
}

static void center(epaper_cards_canvas_t *c, int x, int y, int width, const char *s, int size, card_color_t ink) {
    int text_width = epaper_cards_measure(s, size);
    box(c, x + (text_width < width ? (width - text_width) / 2 : 0), y, width, s, size, ink);
}

// Draws one timeline card, with larger current art and a green completion mark.
// 绘制单个时间线卡片，当前条目使用更大图案，已完成条目显示绿色勾选。
static void timeline_card(epaper_cards_canvas_t *c, const kid_item_t *item, int index, bool current) {
    int x = 12 + index % 5 * 157, y = 82 + index / 5 * 146, w = 148, h = 138;
    static const card_color_t inks[] = {CARD_RED, CARD_BLUE, CARD_GREEN, CARD_BLUE, CARD_RED};
    card_color_t ink = item->done ? CARD_GREEN : current ? CARD_BLUE : inks[index % 5];
    if (current) epaper_cards_rect(c, x, y, w, h, 12, true, CARD_YELLOW);
    for (int i = 0; i < (current ? 3 : 1); i++) epaper_cards_rect(c, x+i, y+i, w-2*i, h-2*i, 12-i, false, current ? CARD_BLUE : ink);
    center(c, x+5, y+3, w-10, item->time, 20, CARD_BLACK);
    int size = item->done || current ? 64 : 48;
    bool icon = epaper_cards_icon_scaled(c, x+(w-size)/2, y+23, item->done ? "check_circle" : item->icon, 64, size, ink);
    if (icon) center(c, x+5, y+81, w-10, item->label, epaper_cards_measure(item->label, 28) <= w-10 ? 28 : 20, CARD_BLACK);
    else epaper_cards_text_box(c, x+8, y+33, w-16, 72, item->label, 28, CARD_BLACK, 2);
    center(c, x+5, y+111, w-10, item->who, 20, CARD_BLACK);
}

// Renders the header, two timeline rows and reward chart in native panel inks.
// 使用面板原生墨色绘制页首、两排时间线及奖励图表。
void kid_model_render(epaper_cards_canvas_t *c, const kid_model_t *m, int64_t now, int offset) {
    epaper_cards_clear(c, CARD_WHITE);
    epaper_cards_rect(c, 0, 0, 800, 72, 0, true, CARD_BLUE);
    box(c, 16, 1, 270, m->child, 40, CARD_WHITE);
    box(c, 18, 45, 265, "KID BOARD", 20, CARD_WHITE);
    char line[96] = "Set clock with Muse", at[8] = "--:--";
    struct tm t; time_t local = (time_t)(now + offset*60LL);
    if (kid_model_clock_valid(now) && gmtime_r(&local, &t)) {
        strftime(line, sizeof(line), "%A %d %b", &t); strftime(at, sizeof(at), "%H:%M", &t);
    }
    box(c, 290, 6, 360, line, 28, CARD_WHITE); box(c, 668, 1, 120, at, 40, CARD_WHITE);
    if (m->date[0]) snprintf(line, sizeof(line), "Plan %s", m->date);
    else strcpy(line, "Ask Muse for your day");
    box(c, 290, 45, 330, line, 20, CARD_WHITE); box(c, 640, 45, 155, "GREEN Done", 20, CARD_WHITE);
    int current = kid_model_current(m, now, offset);
    for (unsigned i = 0; i < m->item_count; i++) timeline_card(c, &m->items[i], (int)i, current == (int)i);
    if (!m->item_count) {
        epaper_cards_icon_scaled(c, 335, 100, "sunny", 64, 112, CARD_YELLOW);
        center(c, 20, 230, 760, "A bright day starts here", 40, CARD_BLUE);
        center(c, 20, 286, 760, "Ask Muse to add your next adventure", 28, CARD_BLACK);
    }
    epaper_cards_hline(c, 16, 374, 768, CARD_BLUE);
    snprintf(line, sizeof(line), "%d / %d stars", m->count, m->goal);
    box(c, 18, 376, 490, line, 28, CARD_BLACK);
    static const card_color_t star_inks[] = {CARD_YELLOW, CARD_RED, CARD_BLUE, CARD_GREEN};
    for (int i = 0; i < m->goal; i++) {
        int x = 18 + i * 25;
        epaper_cards_icon_scaled(c, x, 412, "star", 64, 25, i < m->count ? star_inks[i%4] : CARD_BLACK);
        if (i >= m->count) epaper_cards_icon_scaled(c, x+4, 416, "star", 64, 17, CARD_WHITE);
    }
    bool earned = m->count >= m->goal;
    box(c, 532, 375, 254, earned ? "Reward earned!" : "Working towards", 20, earned ? CARD_GREEN : CARD_BLUE);
    if (!epaper_cards_icon_scaled(c, 530, 400, m->reward_icon, 64, 42, earned ? CARD_GREEN : CARD_RED))
        box(c, 530, 405, 70, "Gift", 20, CARD_BLUE);
    box(c, 580, 400, 204, m->reward_label[0] ? m->reward_label : "Choose a reward", 28, CARD_BLACK);
    box(c, 18, 448, 768, m->note[0] ? m->note : "Every little step counts. We are cheering for you!", 20, CARD_BLUE);
}
