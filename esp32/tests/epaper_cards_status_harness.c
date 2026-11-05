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

#include <assert.h>
#include <setjmp.h>
#include "epaper_status.c"

static unsigned notifications, updates, rendered, max_flight, in_flight;
static bool redraw_during_update, redraw_during_render, image_during_render, animation_during_render, busy;
static uint8_t last_pixel;
static jmp_buf idle;

const uint16_t happy_anim_palette[23] = {0};
const uint8_t happy_anim_frames[HAPPY_ANIM_FRAMES][HAPPY_ANIM_HEIGHT * HAPPY_ANIM_WIDTH] = {{0}};
const uint8_t pixel_font[PIXEL_FONT_LAST - PIXEL_FONT_FIRST + 1][PIXEL_FONT_WIDTH] = {{0}};
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t wait) {(void)h;return !wait && busy ? pdFALSE : pdTRUE;}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {(void)h;return pdTRUE;}
BaseType_t xTaskCreate(TaskFunction_t task, const char *name, unsigned stack,
                       void *arg, unsigned priority, TaskHandle_t *out) {
    (void)task;(void)name;(void)arg;(void)priority;
#if CONFIG_HOMEHUB_KID_BOARD
    assert(stack==4096);
#else
    assert(stack==3072);
#endif
    if(out)*out=(void *)1;return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t h) {(void)h;notifications++;}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks) {
    (void)clear;
    unsigned have=notifications; notifications=0;
    if (!have && ticks==portMAX_DELAY) longjmp(idle,1);
    return have;
}
void stack_monitor_poll(stack_monitor_t *s) {(void)s;}
static esp_err_t panel_init(void) {return ESP_OK;}
static esp_err_t panel_update(const uint8_t *frame, const uint8_t *prev, bool full) {
    (void)prev;
    updates++;in_flight++;if(in_flight>max_flight)max_flight=in_flight;
    if (s_app.active) assert(full);
    last_pixel=frame[0];
    if(redraw_during_update){
        redraw_during_update=false;
        for(int i=0;i<20;i++)assert(epaper_cards_request_redraw());
    }
    in_flight--;return ESP_OK;
}
static epd_panel_t panel={.name="fake",.depth="fake",.width=800,.height=480,.bits_per_pixel=1,
    .anim_scale=1,.title_max_scale=1,.status_scale=1,.anim_y=150,.status_y=400,
    .init=panel_init,.update=panel_update};
const epd_panel_t *epd_panel_get(void) {return &panel;}
static void render(epaper_cards_canvas_t *c,void *ctx) {
    rendered++;assert(ctx==(void *)42);
    epaper_cards_pixel(c,0,0,CARD_BLACK);
    if(redraw_during_render){redraw_during_render=false;assert(epaper_cards_request_redraw());}
    if(image_during_render){
        image_during_render=false;
        uint16_t black=0;
        assert(led_status_draw_rect(0,0,1,1,&black));
    }
    if(animation_during_render){animation_during_render=false;led_status_show_animation();}
}
static void pump(void) {if(!setjmp(idle))epd_task(NULL);}

int main(void) {
    assert(!epaper_cards_request_redraw() && !epaper_cards_show(render,(void *)42));
    assert(led_status_init());pump();assert(updates==1);
    busy=true;assert(!epaper_cards_show(render,(void *)42));assert(!epaper_cards_request_redraw());busy=false;
    assert(epaper_cards_show(render,(void *)42));
    for(int i=0;i<10;i++)assert(epaper_cards_request_redraw());
    pump();assert(updates==2 && rendered==1 && last_pixel==0x7F);
    led_status_set_state(LED_STATE_WS_CONNECTED);led_status_set_title("Muse");
    pump();assert(updates==2 && rendered==1);
    redraw_during_update=true;
    assert(epaper_cards_request_redraw());pump();
    assert(updates==4 && rendered==3 && max_flight==1);
    uint16_t black=0;assert(led_status_draw_rect(0,0,1,1,&black));
    led_status_draw_done();assert(updates==5 && !s_app.active && s_image_mode);
    led_status_set_state(LED_STATE_ERROR);pump();assert(updates==5);
    led_status_show_animation();pump();assert(updates==6 && !s_image_mode);
    assert(!epaper_cards_request_redraw());
    assert(epaper_cards_show(render,(void *)42));image_during_render=true;pump();
    assert(updates==6 && !s_app.active);led_status_draw_done();assert(updates==7);
    assert(epaper_cards_show(render,(void *)42));animation_during_render=true;pump();
    assert(updates==8 && !s_app.active && !s_image_mode);
    assert(epaper_cards_show(render,(void *)42));redraw_during_render=true;
    unsigned before=updates;pump();assert(updates==before+2);
    panel.bits_per_pixel=4;
    assert(led_status_init());pump();
    assert(epaper_cards_show(render,(void *)42));pump();assert(last_pixel==0x01);
    puts("ok");return 0;
}
