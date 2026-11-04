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

#include "esp_timer.h"
#include "muse_state.h"

static lv_obj_t *s_root, *s_block, *s_owner, *s_icon, *s_title, *s_until, *s_detail;
static lv_obj_t *s_buttons[DOOR_SIGN_REPLIES], *s_labels[DOOR_SIGN_REPLIES];
static door_sign_snapshot_t s_shown;
static bool s_have_shown;
static int64_t s_second = -1;
static bool s_sent;
static const char *const s_icons[] = {LV_SYMBOL_VIDEO, LV_SYMBOL_EDIT, LV_SYMBOL_OK, LV_SYMBOL_HOME, LV_SYMBOL_LIST};

#if LV_FONT_MONTSERRAT_12
#define FONT_SMALL (&lv_font_montserrat_12)
#else
#define FONT_SMALL (&lv_font_montserrat_14)
#endif

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, int h, const lv_font_t *font)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_white(), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void on_reply(lv_event_t *event)
{
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    muse_state_poke();
    if (s_have_shown) door_sign_reply(index, s_shown.revision);
}

// Fit controls inside a safe central square on round displays.
// 在圆形屏幕中央的安全方形区域内布置控件。
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
    int h = round ? w : height - 24;
    int x = (width - w) / 2, y = round ? (height - h) / 2 : 4;
    bool wide = w > h * 12 / 10;
    int card_w = wide ? w / 2 - 4 : w;
    int card_h = wide ? h - 26 : h - 176;
    if (card_h < 90) card_h = 90;
    s_owner = label(s_root, x, y, w, 18, FONT_SMALL);
    s_block = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_block);
    lv_obj_set_pos(s_block, x, y + 22);
    lv_obj_set_size(s_block, card_w, card_h);
    lv_obj_set_style_bg_opa(s_block, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_block, 14, 0);
    lv_obj_remove_flag(s_block, LV_OBJ_FLAG_SCROLLABLE);
    s_icon = label(s_block, 4, 6, card_w - 8, 22, &lv_font_montserrat_20);
    s_title = label(s_block, 6, 31, card_w - 12, card_h - 68, &lv_font_montserrat_20);
    s_until = label(s_block, 2, card_h - 35, card_w - 4, 34, FONT_SMALL);
    int buttons_y = wide ? y + 28 : y + h - 116;
    int buttons_x = wide ? x + w / 2 + 4 : x;
    int buttons_w = wide ? w / 2 - 4 : w;
    s_detail = label(s_root, wide ? buttons_x : x,
                     wide ? y + h - 38 : y + 26 + card_h, wide ? buttons_w : w, 32, FONT_SMALL);
    for (unsigned i = 0; i < DOOR_SIGN_REPLIES; i++) {
        s_buttons[i] = lv_button_create(s_root);
        lv_obj_remove_style_all(s_buttons[i]);
        lv_obj_set_pos(s_buttons[i], buttons_x, buttons_y + (int)i * 40);
        lv_obj_set_size(s_buttons[i], buttons_w, 36);
        lv_obj_set_style_radius(s_buttons[i], 9, 0);
        lv_obj_set_style_bg_color(s_buttons[i], lv_color_hex(0x293442), 0);
        lv_obj_set_style_bg_color(s_buttons[i], lv_color_hex(0x346e59), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(s_buttons[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(s_buttons[i], LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(s_buttons[i], on_reply, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        s_labels[i] = label(s_buttons[i], 4, 2, buttons_w - 8, LV_SIZE_CONTENT, &lv_font_montserrat_14);
        lv_obj_set_style_max_height(s_labels[i], 32, 0);
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
        char owner[48];
        snprintf(owner, sizeof(owner), "%s's door", view.sign.owner);
        lv_label_set_text(s_owner, owner);
        lv_obj_set_style_bg_color(s_block, lv_color_hex(view.sign.color), 0);
        unsigned color = view.sign.color;
        unsigned luminance = 299 * (color >> 16) + 587 * ((color >> 8) & 255) + 114 * (color & 255);
        lv_color_t ink = luminance > 145000 ? lv_color_black() : lv_color_white();
        lv_obj_set_style_text_color(s_title, ink, 0);
        lv_obj_set_style_text_color(s_icon, ink, 0);
        lv_obj_set_style_text_color(s_until, ink, 0);
        lv_label_set_text(s_icon, s_icons[view.sign.state]);
        lv_label_set_text(s_title, view.sign.title);
        for (unsigned i = 0; i < DOOR_SIGN_REPLIES; i++) {
            lv_obj_set_flag(s_buttons[i], LV_OBJ_FLAG_HIDDEN, i >= view.sign.reply_count);
            lv_label_set_text(s_labels[i], view.sign.replies[i]);
        }
    }
    if (changed || s_sent != sent || s_shown.sent_us != view.sent_us) {
        char detail[64];
        snprintf(detail, sizeof(detail), "Sent to %s", view.sign.owner);
        lv_label_set_text(s_detail, sent ? detail : view.sign.subtitle[0] ? view.sign.subtitle : "Hold talk to leave a note");
        for (unsigned i = 0; i < DOOR_SIGN_REPLIES; i++)
            lv_obj_set_style_bg_color(s_buttons[i], lv_color_hex(sent && view.sent_reply == (int)i ? 0x346e59 : 0x293442), 0);
    }
    if (changed || s_second != now_us / 1000000 || s_shown.clock_valid != view.clock_valid) {
        char until[80] = "";
        if (view.sign.until && !view.clock_valid) strcpy(until, "Waiting for clock");
        else if (view.sign.until) {
            int64_t remaining = LV_MAX(0, view.sign.until - view.epoch);
            int second = (int)((view.local_second + view.sign.until - view.epoch) % 86400);
            if (second < 0) second += 86400;
            snprintf(until, sizeof(until), "until %02d:%02d\n%lldh %02lldm left", second / 3600, second / 60 % 60,
                     (long long)(remaining / 3600), (long long)(remaining / 60 % 60));
        }
        lv_label_set_text(s_until, until);
    }
    s_second = now_us / 1000000;
    s_sent = sent;
    s_shown = view;
    s_have_shown = true;
    return true;
}
