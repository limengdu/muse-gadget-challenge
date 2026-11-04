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

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "sim_board.h"
#include "sim_platform.h"
#include "sim_services.h"
#include "muse_ui.h"
#include "muse_state.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "../../components/muse/muse_door_sign.c"

static door_sign_snapshot_t model;
static unsigned replies;

bool door_sign_snapshot(door_sign_snapshot_t *out) { *out = model; return true; }
bool door_sign_reply(unsigned index, uint32_t revision)
{
    assert(index < model.sign.reply_count && revision == model.revision);
    replies++;
    model.sent_reply = (int)index;
    model.sent_us = esp_timer_get_time();
    return true;
}

static void advance(unsigned duration)
{
    for (unsigned i = 0; i < duration; i += 5) {
        sim_time_advance_us(5000);
        lv_timer_handler();
    }
}

static void inside(lv_obj_t *obj, int w, int h)
{
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    assert(area.x1 >= 0 && area.y1 >= 0 && area.x2 < w && area.y2 < h);
    assert(area.x1 <= area.x2 && area.y1 <= area.y2);
}

static void snapshot(const char *directory, const char *name)
{
    lv_display_t *display = sim_board_display();
    lv_refr_now(display);
    SDL_Renderer *renderer = lv_sdl_window_get_renderer(display);
    int w, h;
    assert(SDL_GetRendererOutputSize(renderer, &w, &h) == 0);
    unsigned char *pixels = malloc((size_t)w * h * 3);
    assert(pixels && SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGB24, pixels, w * 3) == 0);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n%d %d\n255\n", w, h);
    assert(fwrite(pixels, 3, (size_t)w * h, file) == (size_t)w * h);
    assert(fclose(file) == 0);
    free(pixels);
}

static lv_point_t pointer;
static bool pressed;
static void read_pointer(lv_indev_t *input, lv_indev_data_t *data)
{
    (void)input;
    data->point = pointer;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void tap(lv_indev_t *input, lv_obj_t *obj)
{
    lv_area_t bounds;
    lv_obj_get_coords(obj, &bounds);
    pointer = (lv_point_t){(bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2};
    pressed = true; lv_indev_read(input); advance(60);
    pressed = false; lv_indev_read(input); advance(60);
}

static void swipe_left(lv_indev_t *input, int w, int h)
{
    pointer = (lv_point_t){w - 20, h / 2};
    pressed = true; lv_indev_read(input); advance(40);
    for (int i = 1; i <= 12; i++) {
        pointer.x = w - 20 - (w - 40) * i / 12;
        lv_indev_read(input); advance(20);
    }
    pressed = false; lv_indev_read(input); advance(600);
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    int w = atoi(argv[1]), h = atoi(argv[2]);
    bool round = atoi(argv[3]) != 0;
    sim_time_reset();
    sim_services_reset();
    lv_init();
    sim_board_configure(w, h, round);
    muse_board = sim_board_get();
    muse_state_init();
    muse_state_set_mode(MUSE_MODE_IDLE);
    model = (door_sign_snapshot_t){
        .sign = {.active = true, .state = DOOR_SIGN_MEETING, .color = 0xb52b36,
            .title = "In a meeting", .subtitle = "Please leave a message", .owner = "Dad",
            .reply_count = 3, .replies = {"Dinner is ready", "Need you", "Parcel arrived"}, .until = 1800001800},
        .revision = 1, .epoch = 1800000000, .local_second = 14 * 3600 + 30 * 60, .clock_valid = true,
        .sent_us = -1, .sent_reply = -1,
    };
    assert(muse_ui_start() == ESP_OK);
    advance(300);
    assert(s_have_shown && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN));
    lv_obj_update_layout(s_root);
    inside(s_owner, w, h); inside(s_block, w, h); inside(s_title, w, h); inside(s_detail, w, h);
    for (unsigned i = 0; i < 3; i++) {
        inside(s_buttons[i], w, h);
        inside(s_labels[i], w, h);
        lv_area_t bounds, prior;
        lv_obj_get_coords(s_buttons[i], &bounds);
        lv_obj_get_coords(i ? s_buttons[i-1] : s_detail, &prior);
        if (w <= h) assert(prior.y2 < bounds.y1);
    }
    assert(strstr(lv_label_get_text(s_until), "until 15:00"));
    snapshot(argv[4], "meeting");
    lv_indev_t *input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, read_pointer);
    lv_indev_set_display(input, sim_board_display());
    tap(input, s_buttons[0]);
    assert(replies == 1 && !strcmp(lv_label_get_text(s_detail), "Sent to Dad"));
    snapshot(argv[4], "sent");
    advance(3200);
    assert(!strcmp(lv_label_get_text(s_detail), model.sign.subtitle));
    muse_state_set_mode(MUSE_MODE_LISTENING); advance(300);
    assert(lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN));
    muse_state_set_mode(MUSE_MODE_SPEAKING); muse_state_set_caption("Dad will come soon."); advance(300);
    assert(lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN));
    muse_state_set_caption(""); muse_state_set_mode(MUSE_MODE_IDLE); advance(300);
    assert(!lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN));
    swipe_left(input, w, h);
    assert(!muse_ui_face_active());
    muse_state_set_asleep(true); advance(300); assert(muse_ui_dark());
    muse_state_set_asleep(false); advance(600);
    assert(!muse_ui_dark() && muse_ui_face_active() && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN));
    model.sign.active = false; model.revision++; advance(300);
    assert(lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN) && muse_ui_face_active());
    snapshot(argv[4], "face");
    model.sign.active = true; model.sign.state = DOOR_SIGN_CUSTOM; model.sign.color = 0xffffff;
    strcpy(model.sign.title, "Please come back after my next meeting");
    strcpy(model.sign.subtitle, "Leave a note for me here");
    strcpy(model.sign.replies[0], "Please come downstairs now");
    model.revision++; advance(300);
    snapshot(argv[4], "custom");
    model.sign.reply_count = 0; model.revision++; advance(300);
    for (unsigned i = 0; i < 3; i++) assert(lv_obj_has_flag(s_buttons[i], LV_OBJ_FLAG_HIDDEN));
    lv_indev_delete(input);
    lv_deinit(); SDL_Quit();
    puts("Door sign: geometry, pointer tap, confirmation, voice view, settings, sleep/wake and return to face passed");
    return 0;
}
