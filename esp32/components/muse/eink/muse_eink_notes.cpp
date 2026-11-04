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

#include "muse_eink_notes.h"
extern "C" {
#include "sticky_notes.h"
#include "muse_state.h"
}
#include <stdio.h>
#include <string.h>

namespace muse_eink {
using freeink::ui::ActionEvent;
enum : freeink::ui::ActionId { ACT_NOTES_TAB = 50, ACT_NOTES_ROW, ACT_NOTES_CLEAR,
                               ACT_NOTES_PREV, ACT_NOTES_NEXT, ACT_NOTES_FACE };
static sticky_notes_view_t s_view;
static unsigned s_selected, s_first;
static bool s_busy;
static constexpr unsigned ROWS = 7;

bool notes_available(void) {
    sticky_notes_view_t view;
    return sticky_notes_view(s_selected, &view) && view.count > 0;
}

// Refreshes revised content, retries busy reads and fences moved row IDs.
// 更新变更内容、重试繁忙的读取，并隔离行编号移动前的旧触摸。
void notes_prepare(void) {
    sticky_notes_view_t next;
    if (!sticky_notes_view(s_selected, &next)) {
        request_draw();
        return;
    }
    bool moved = next.count != s_view.count || next.selected != s_view.selected
        || next.list.count != s_view.list.count
        || memcmp(next.names, s_view.names, sizeof(next.names)) != 0;
    for (unsigned i = 0; !moved && i < next.list.count; i++)
        if (next.list.items[i].id != s_view.list.items[i].id) moved = true;
    bool revised = next.revision != s_view.revision;
    if (moved && s_view.count) navigation_changed();
    if (moved || revised) request_draw();
    if (revised) s_busy = false;
    s_view = next;
    s_selected = s_view.selected;
    if (s_first >= s_view.list.count) s_first = 0;
}

void notes_home(void) { s_first = 0; }

static void changed(void) {
    navigation_changed();
    request_draw();
}

int notes_swipe(int dx) {
    if (dx < 0) {
        if (s_selected + 1 >= s_view.count) return 1;
        s_selected++;
    } else {
        if (!s_selected) return -1;
        s_selected--;
    }
    s_first = 0;
    notes_prepare();
    changed();
    return 0;
}

void notes_scroll(int dy) {
    if (dy < 0 && s_first + ROWS < s_view.list.count) s_first += ROWS;
    else if (dy > 0 && s_first >= ROWS) s_first -= ROWS;
    else return;
    changed();
}

static void on_notes(const ActionEvent &event, void *) {
    switch (event.action) {
    case ACT_NOTES_TAB:
        if (event.value < 0 || (unsigned)event.value >= s_view.count) return;
        s_selected = (unsigned)event.value;
        s_first = 0;
        notes_prepare();
        changed();
        break;
    case ACT_NOTES_ROW:
        if (event.value < 0 || (unsigned)event.value >= s_view.list.count) return;
        s_busy = !sticky_notes_touch(s_view.list.name, s_view.list.items[event.value].id, false);
        request_draw();
        break;
    case ACT_NOTES_CLEAR:
        s_busy = !sticky_notes_touch(s_view.list.name, 0, true);
        request_draw();
        break;
    case ACT_NOTES_PREV: notes_scroll(100); break;
    case ACT_NOTES_NEXT: notes_scroll(-100); break;
    case ACT_NOTES_FACE: request_face(); break;
    default: break;
    }
}

void notes_setup(App &app) {
    for (unsigned action = ACT_NOTES_TAB; action <= ACT_NOTES_FACE; action++) app.on(action, on_notes);
    sticky_notes_watch(request_draw);
}

// Draws the selected list and publishes hit regions for its visible rows.
// 绘制选定清单并发布可见行的触摸区域。
void notes_screen(Screen &screen) {
    notes_prepare();
    auto &target = screen.target();
    auto &frame = screen.frame();
    draw_text(16, 16, FONT_M28, "Fridge Notes");
    Rect face{380, 8, 84, 44};
    target.stroke(face, Paint::solid(Color::Black), 1, 8);
    draw_text_centered(422, 20, FONT_M16, "Muse");
    frame.hit(face, ACT_NOTES_FACE);
    const int tab_w = (g_w - 16) / (int)(s_view.count ? s_view.count : 1);
    for (unsigned i = 0; i < s_view.count; i++) {
        int x = 8 + (int)i * tab_w;
        Rect tab{(int16_t)x, 64, (int16_t)(tab_w - 4), 56};
        target.stroke(tab, Paint::solid(Color::Black), i == s_selected ? 3 : 1, 6);
        draw_wrapped(x + 6, 80, tab_w - 16, FONT_M16, s_view.titles[i], 1, 0, true);
        frame.hit(tab, ACT_NOTES_TAB, (int16_t)i);
    }
    for (unsigned row = 0; row < ROWS && s_first + row < s_view.list.count; row++) {
        unsigned i = s_first + row;
        const auto &item = s_view.list.items[i];
        int y = 136 + (int)row * 74;
        Color color = item.done ? Color::DarkGray : Color::Black;
        Rect box{16, (int16_t)(y + 12), 36, 36};
        // Checkbox outlines and checkmarks are solid black in both row states.
        // 勾选与未勾选状态的方框及勾号均使用纯黑色。
        target.stroke(box, Paint::solid(Color::Black), 2, 4);
        if (item.done) {
            target.line({22, (int16_t)(y + 30)}, {30, (int16_t)(y + 38)}, 3, Paint::solid(Color::Black));
            target.line({30, (int16_t)(y + 38)}, {46, (int16_t)(y + 20)}, 3, Paint::solid(Color::Black));
        }
        int lines = draw_wrapped(66, y + 6, g_w - 82, FONT_M20, item.text, 2, 2, false, color);
        if (item.done) for (int n = 0; n < lines; n++) {
            int line_y = y + 6 + n * (line_height(FONT_M20) + 2) + line_height(FONT_M20) / 2;
            target.line({66, (int16_t)line_y}, {(int16_t)(g_w - 16), (int16_t)line_y}, 1, Paint::solid(color));
        }
        if (item.who[0]) draw_wrapped(66, y + 54, g_w - 82, FONT_M14, item.who, 1, 0, false, color);
        frame.hit(Rect{8, (int16_t)y, (int16_t)(g_w - 16), 74}, ACT_NOTES_ROW, (int16_t)i);
    }
    if (!s_view.list.count) draw_text_centered(g_w / 2, 250, FONT_M20, "Hold AI to add a note");
    if (s_first) {
        draw_text(16, 674, FONT_M16, "Previous");
        frame.hit(Rect{8, 656, 120, 44}, ACT_NOTES_PREV);
    }
    if (s_first + ROWS < s_view.list.count) {
        draw_text(g_w - 66, 674, FONT_M16, "Next");
        frame.hit(Rect{(int16_t)(g_w - 128), 656, 120, 44}, ACT_NOTES_NEXT);
    }
    char position[32];
    snprintf(position, sizeof(position), "%u / %u", s_view.list.count ? s_first / ROWS + 1 : 1,
             s_view.list.count ? (s_view.list.count + ROWS - 1) / ROWS : 1);
    draw_text_centered(g_w / 2, 674, FONT_M16, position);
    Rect clear{130, 706, 220, 48};
    target.stroke(clear, Paint::solid(Color::Black), 1, 8);
    draw_text_centered(g_w / 2, 720, FONT_M20, "Clear done");
    frame.hit(clear, ACT_NOTES_CLEAR);
    muse_power_t power = muse_state_power();
    char battery[24], sync[64];
    if (power.battery_pct < 0) snprintf(battery, sizeof(battery), "Battery --");
    else snprintf(battery, sizeof(battery), "Battery %d%%", power.battery_pct);
    draw_text(16, 776, FONT_M14, battery);
    if (s_busy || s_view.save_failed) snprintf(sync, sizeof(sync), "%s", s_busy ? "Busy: tap again" : "Save failed: retry");
    else if (s_view.synced_minute < 0) snprintf(sync, sizeof(sync), "%s", s_view.pending ? "Sync pending" : "Synced --:--");
    else snprintf(sync, sizeof(sync), "Synced %02d:%02d%s", s_view.synced_minute / 60,
                  s_view.synced_minute % 60, s_view.pending ? " *" : "");
    draw_text(g_w - text_width(FONT_M14, sync) - 16, 776, FONT_M14, sync);
}
}
