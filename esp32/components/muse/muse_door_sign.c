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

#include "muse_door_sign_view.h"
#include "muse_door_sign.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_timer.h"
#include "muse_state.h"

static lv_obj_t *s_root, *s_block, *s_owner, *s_icon, *s_title, *s_until;
static lv_obj_t *s_buttons[DOOR_SIGN_REPLIES], *s_labels[DOOR_SIGN_REPLIES];
static door_sign_snapshot_t s_shown;
static bool s_have_shown;
static int64_t s_second = -1;
static bool s_sent;
static const struct {
    const char *stored_title, *word, *icon;
} s_states[] = {
    {"In a meeting", "Meeting", LV_SYMBOL_VIDEO},
    {"Focusing", "Focus", LV_SYMBOL_EDIT},
    {"Come in", "Free", LV_SYMBOL_OK},
    {"Away", "Away", LV_SYMBOL_HOME},
    {"My door", "Custom", LV_SYMBOL_LIST},
};

static const struct {
    const char *reply, *word, *icon;
} s_replies[] = {
    {"Dinner is ready", "Dinner", LV_SYMBOL_HOME},
    {"Dinner", "Dinner", LV_SYMBOL_HOME},
    {"Come home", "Come home", LV_SYMBOL_HOME},
    {"Need you", "Need you", LV_SYMBOL_BELL},
    {"Parcel arrived", "Parcel", LV_SYMBOL_ENVELOPE},
    {"Parcel", "Parcel", LV_SYMBOL_ENVELOPE},
    {"Call me", "Call me", LV_SYMBOL_CALL},
    {"OK", "OK", LV_SYMBOL_OK},
    {"Okay", "OK", LV_SYMBOL_OK},
    {"Come here", "Come here", LV_SYMBOL_BELL},
    {"I'm home", "Home", LV_SYMBOL_HOME},
};

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, int h, const lv_font_t *font)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_white(), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(obj, 0, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

// Measure incoming text within a label's width and line limit.
// 按标签宽度和行数上限测量传入的完整文字。
static bool text_fits(lv_obj_t *obj, const char *text, const lv_font_t *font,
                      unsigned lines, lv_point_t *size)
{
    int width = lv_obj_get_content_width(obj);
    int line_space = lv_obj_get_style_text_line_space(obj, 0);
    lv_text_get_size(size, text, font, lv_obj_get_style_text_letter_space(obj, 0),
                     line_space, width, LV_TEXT_FLAG_NONE);
    return size->x <= width && size->y <= (int)lines * font->line_height + ((int)lines - 1) * line_space;
}

// Select the largest fitting font and apply the label's line limit.
// 选择能完整显示文字的最大字号，并设置标签的行数上限。
static void fit_text(lv_obj_t *obj, const char *text, const lv_font_t *large,
                     const lv_font_t *small, unsigned large_lines, unsigned small_lines)
{
    lv_obj_update_layout(obj);
    const lv_font_t *font = large;
    unsigned lines = large_lines;
    lv_point_t size;
    bool fits = text_fits(obj, text, font, lines, &size);
    if (!fits && small != large) {
        font = small;
        lines = small_lines;
        fits = text_fits(obj, text, font, lines, &size);
    }
    lv_obj_set_style_text_font(obj, font, 0);
    int height = fits ? size.y : (int)lines * font->line_height +
                                ((int)lines - 1) * lv_obj_get_style_text_line_space(obj, 0);
    lv_obj_set_height(obj, LV_MAX(font->line_height, height));
    lv_label_set_long_mode(obj, fits ? LV_LABEL_LONG_MODE_WRAP : LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(obj, text);
}

// Map reply payloads to compact symbols and words, retaining their tap index.
// 将回复内容映射为简短图标和文字，并保留对应的点击索引。
static void set_reply_label(unsigned index, const char *reply, bool sent)
{
    char text[64];
    snprintf(text, sizeof(text), "%s", reply);
    for (unsigned i = 0; i < sizeof(s_replies) / sizeof(s_replies[0]); i++) {
        if (!strcasecmp(reply, s_replies[i].reply)) {
            snprintf(text, sizeof(text), "%s %s", s_replies[i].icon, s_replies[i].word);
            break;
        }
    }
    if (sent) snprintf(text, sizeof(text), "%s Sent", LV_SYMBOL_OK);
    lv_obj_t *obj = s_labels[index];
    lv_obj_update_layout(obj);
    const lv_font_t *large = &lv_font_montserrat_20;
    for (const char *start = text; *start;) {
        if (*start == ' ') { start++; continue; }
        size_t length = strcspn(start, " ");
        char word[sizeof(text)];
        memcpy(word, start, length);
        word[length] = '\0';
        lv_point_t size;
        lv_text_get_size(&size, word, large, lv_obj_get_style_text_letter_space(obj, 0),
                         0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > lv_obj_get_content_width(obj)) large = &lv_font_montserrat_16;
        start += length;
    }
    fit_text(obj, text, large, &lv_font_montserrat_16, 2, 2);
}

static void on_reply(lv_event_t *event)
{
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    muse_state_poke();
    if (s_have_shown) door_sign_reply(index, s_shown.revision);
}

// Reserve the bottom page dots and fit round displays inside a central square.
// 为底部分页点预留空间，并在圆屏的中央方形区域内布置控件。
void door_sign_view_create(lv_obj_t *parent, int width, int height, bool round)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, width, height);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x10151e), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    int w = round ? LV_MIN(width, height) * 68 / 100 : width - 16;
    int h = round ? w : height - 32;
    int x = (width - w) / 2, y = round ? (height - h) / 2 : 4;
    bool wide = w > h * 12 / 10;
    int card_w = wide ? w / 2 - 4 : w;
    int buttons_y = wide ? y + 22 + (h - 162) / 2 : y + h - 140;
    int buttons_x = wide ? x + w / 2 + 4 : x;
    int buttons_w = wide ? w / 2 - 4 : w;
    int card_h = wide ? h - 22 : buttons_y - y - 26;
    s_owner = label(s_root, x, y, w, 20, &lv_font_montserrat_16);
    lv_obj_set_style_text_color(s_owner, lv_color_hex(0xc3c7ce), 0);
    s_block = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_block);
    lv_obj_set_pos(s_block, x, y + 22);
    lv_obj_set_size(s_block, card_w, card_h);
    lv_obj_set_style_bg_opa(s_block, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_block, 14, 0);
    lv_obj_remove_flag(s_block, LV_OBJ_FLAG_SCROLLABLE);
    s_icon = label(s_block, 4, 6, card_w - 8, 30, &lv_font_montserrat_28);
    s_title = label(s_block, 6, 40, card_w - 12, 44, &lv_font_montserrat_28);
    s_until = label(s_block, 2, card_h - 26, card_w - 4, 22, &lv_font_montserrat_20);
    for (unsigned i = 0; i < DOOR_SIGN_REPLIES; i++) {
        s_buttons[i] = lv_button_create(s_root);
        lv_obj_remove_style_all(s_buttons[i]);
        lv_obj_set_pos(s_buttons[i], buttons_x, buttons_y + (int)i * 48);
        lv_obj_set_size(s_buttons[i], buttons_w, 44);
        lv_obj_set_style_radius(s_buttons[i], 9, 0);
        lv_obj_set_style_bg_color(s_buttons[i], lv_color_hex(0x293442), 0);
        lv_obj_set_style_bg_color(s_buttons[i], lv_color_hex(0x346e59), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(s_buttons[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(s_buttons[i], LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(s_buttons[i], on_reply, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        s_labels[i] = label(s_buttons[i], 4, 2, buttons_w - 8, 22, &lv_font_montserrat_20);
        lv_obj_center(s_labels[i]);
    }
}

// Refresh changed sign widgets and the short-lived reply confirmation.
// 更新变化的门牌控件和短暂显示的回复确认。
bool door_sign_view_update(bool available)
{
    if (!s_root) return false;
    if (!available) {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        return false;
    }
    door_sign_snapshot_t view;
    if (!door_sign_snapshot(&view)) return !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    bool visible = available && view.sign.active;
    lv_obj_set_flag(s_root, LV_OBJ_FLAG_HIDDEN, !visible);
    if (!visible) return false;
    int64_t now_us = esp_timer_get_time();
    bool sent = view.sent_us >= 0 && now_us >= view.sent_us && now_us - view.sent_us < 3000000;
    bool changed = !s_have_shown || s_shown.revision != view.revision;
    if (changed) {
        lv_label_set_text(s_owner, view.sign.owner);
        lv_obj_set_style_bg_color(s_block, lv_color_hex(view.sign.color), 0);
        unsigned color = view.sign.color;
        unsigned luminance = 299 * (color >> 16) + 587 * ((color >> 8) & 255) + 114 * (color & 255);
        lv_color_t ink = luminance > 145000 ? lv_color_black() : lv_color_white();
        lv_obj_set_style_text_color(s_title, ink, 0);
        lv_obj_set_style_text_color(s_icon, ink, 0);
        lv_obj_set_style_text_color(s_until, ink, 0);
        lv_label_set_text(s_icon, s_states[view.sign.state].icon);
        const char *title = !strcmp(view.sign.title, s_states[view.sign.state].stored_title)
                          ? s_states[view.sign.state].word : view.sign.title;
        fit_text(s_title, title, &lv_font_montserrat_28, &lv_font_montserrat_20, 1, 2);
        lv_obj_set_y(s_title, 40 + (lv_obj_get_height(s_block) - 70 - lv_obj_get_style_height(s_title, 0)) / 2);
        for (unsigned i = 0; i < DOOR_SIGN_REPLIES; i++) {
            lv_obj_set_flag(s_buttons[i], LV_OBJ_FLAG_HIDDEN, i >= view.sign.reply_count);
        }
    }
    if (changed || s_sent != sent || s_shown.sent_us != view.sent_us) {
        for (unsigned i = 0; i < DOOR_SIGN_REPLIES; i++) {
            bool selected = sent && view.sent_reply == (int)i;
            set_reply_label(i, view.sign.replies[i], selected);
            lv_obj_set_style_bg_color(s_buttons[i], lv_color_hex(selected ? 0x346e59 : 0x293442), 0);
        }
    }
    if (changed || s_second != now_us / 1000000 || s_shown.clock_valid != view.clock_valid) {
        char until[80] = "";
        if (view.sign.until && !view.clock_valid) strcpy(until, "Sync clock");
        else if (view.sign.until) {
            int second = (int)((view.local_second + view.sign.until - view.epoch) % 86400);
            if (second < 0) second += 86400;
            snprintf(until, sizeof(until), "until %02d:%02d", second / 3600, second / 60 % 60);
            lv_point_t size;
            if (!text_fits(s_until, until, &lv_font_montserrat_20, 1, &size))
                snprintf(until, sizeof(until), "%s %02d:%02d", LV_SYMBOL_RIGHT, second / 3600, second / 60 % 60);
        }
        lv_label_set_text(s_until, until);
    }
    s_second = now_us / 1000000;
    s_sent = sent;
    s_shown = view;
    s_have_shown = true;
    return true;
}
