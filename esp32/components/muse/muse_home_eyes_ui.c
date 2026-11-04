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

#include "muse_home_eyes_ui.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"
#include "muse_board.h"
#include "muse_ui.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static muse_home_eyes_view_t s_view;
static atomic_uint s_ack;
static atomic_uint s_dismissed;
static atomic_bool s_ready;
static void (*s_notify)(void);
static lv_obj_t *s_badge, *s_card, *s_heading, *s_body, *s_time;
static uint32_t s_drawn;
static bool s_alert;
static char s_badge_text[112];

void muse_home_eyes_bind(void (*notify)(void)) { s_notify = notify; }
void muse_home_eyes_ready(void) { atomic_store(&s_ready, true); }
bool muse_home_eyes_is_ready(void) { return atomic_load(&s_ready); }
uint32_t muse_home_eyes_take_ack(void) { return atomic_exchange(&s_ack, 0); }

void muse_home_eyes_publish(const muse_home_eyes_view_t *view)
{
    portENTER_CRITICAL(&s_lock); s_view = *view; portEXIT_CRITICAL(&s_lock);
}

// Consume one wheel gesture for the card currently presented on screen.
// 将一次滚轮按键用于当前屏幕卡片的确认。
bool muse_home_eyes_dismiss(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t id = s_view.card_id;
    portEXIT_CRITICAL(&s_lock);
    if (!id || atomic_load(&s_dismissed) == id) return false;
    atomic_store(&s_dismissed, id);
    atomic_store(&s_ack, id);
    if (s_notify) s_notify();
    return true;
}

#if CONFIG_MUSE_CJK_FONT_NOTO
LV_FONT_DECLARE(muse_font_cjk_noto_16)
#define HOME_EYES_CJK_FONT muse_font_cjk_noto_16
#elif CONFIG_MUSE_CJK_FONT
LV_FONT_DECLARE(muse_font_cjk_16)
#define HOME_EYES_CJK_FONT muse_font_cjk_16
#endif

static lv_font_t s_heading_font, s_body_font;

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, int width)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    return obj;
}

static void build(void)
{
    s_heading_font = lv_font_montserrat_20;
    s_body_font = lv_font_montserrat_24;
#if CONFIG_MUSE_CJK_FONT
    s_heading_font.fallback = &HOME_EYES_CJK_FONT;
    s_body_font.fallback = &HOME_EYES_CJK_FONT;
#endif
    s_badge = label(lv_layer_top(), LV_FONT_DEFAULT, 260);
    lv_obj_align(s_badge, LV_ALIGN_BOTTOM_MID, 0, -46);
    lv_obj_set_style_bg_color(s_badge, lv_color_hex(0x163e36), 0);
    lv_obj_set_style_bg_opa(s_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_badge, 12, 0);
    lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    s_card = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_card, muse_board->width, muse_board->height);
    lv_obj_center(s_card);
    lv_obj_set_style_radius(s_card, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_card, lv_color_hex(0x102124), 0);
    lv_obj_set_style_bg_opa(s_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_card, 6, 0);
    lv_obj_set_style_pad_all(s_card, 0, 0);
    lv_obj_remove_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
    s_heading = label(s_card, &s_heading_font, 260);
    lv_label_set_long_mode(s_heading, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(s_heading, LV_ALIGN_TOP_MID, 0, 48);
    s_time = label(s_card, LV_FONT_DEFAULT, 240);
    lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_t *scroll = lv_obj_create(s_card);
    lv_obj_set_size(scroll, 294, 194);
    lv_obj_align(scroll, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 4, 0);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    s_body = label(scroll, &s_body_font, 274);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(s_body, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *hint = label(s_card, LV_FONT_DEFAULT, 240);
    lv_label_set_text(hint, "PRESS WHEEL TO ACKNOWLEDGE");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -56);
    lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN);
}

// Update only changed card content; calculate the badge countdown locally.
// 仅更新变化的卡片内容，并在界面内计算徽标倒计时。
void muse_home_eyes_tick(void)
{
    muse_home_eyes_view_t view;
    portENTER_CRITICAL(&s_lock); view = s_view; portEXIT_CRITICAL(&s_lock);
    if (!s_card) build();
    if (view.active && muse_ui_face_active()) {
        char text[112];
        int64_t left = (view.next_ms - esp_timer_get_time() / 1000 + 999) / 1000;
        if (left < 0) left = 0;
        if (view.waiting) snprintf(text, sizeof(text), LV_SYMBOL_EYE_OPEN " watching %.48s\nWaiting for Muse", view.name);
        else snprintf(text, sizeof(text), LV_SYMBOL_EYE_OPEN " watching %.48s\nNext check %02lld:%02lld", view.name,
                      (long long)(left / 60), (long long)(left % 60));
        if (strcmp(text, s_badge_text)) {
            strcpy(s_badge_text, text); lv_label_set_text(s_badge, text);
        }
        lv_obj_remove_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    if (!view.card_id || view.card_id == atomic_load(&s_dismissed)) {
        lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN); return;
    }
    if (view.card_id != s_drawn || s_alert != view.alert) {
        s_drawn = view.card_id; s_alert = view.alert;
        muse_ui_show_face();
        lv_label_set_text(s_heading, view.heading);
        lv_label_set_text(s_body, view.text);
        lv_label_set_text(s_time, view.time);
        lv_obj_scroll_to_y(lv_obj_get_parent(s_body), 0, LV_ANIM_OFF);
        lv_obj_set_style_border_color(s_card, lv_color_hex(view.alert ? 0xf04747 : 0x4fbaad), 0);
    }
    lv_obj_remove_flag(s_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_card);
}
