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

#include <FreeInkApp.h>
#include <assert.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
extern "C" {
#include "sticky_notes.h"
}
using namespace freeink::ui;
namespace muse_eink {
using App = FreeInkApp<160,96>;
using Screen = App::ScreenType;
enum : FontId { FONT_UNSCII8, FONT_UNSCII16, FONT_M14, FONT_M16, FONT_M20, FONT_M28 };
int g_w=480, g_h=800;
App *g_app;
struct muse_power_t {int battery_pct;};
static muse_power_t muse_state_power() {return {80};}
static int line_height(FontId) {return 22;}
static int text_width(FontId,const char *s,int=0) {return (int)strlen(s)*7;}
static void draw_text(int,int,FontId,const char *,Color=Color::Black,int=0) {}
static void draw_text_centered(int,int,FontId,const char *,Color=Color::Black,int=0) {}
static int draw_wrapped(int,int,int,FontId,const char *,int,int,bool,Color=Color::Black) {return 1;}
static std::atomic<uint32_t> s_navigation_epoch{};
static bool s_settings, s_notes, s_return_notes, s_had_notes, s_saw_reply, s_notes_home_pending;
static bool s_face_active, s_need_clean, s_draw_now, s_mic_visible;
static bool face_requested;
static void request_draw() {s_draw_now=true;}
static void navigation_changed() {s_navigation_epoch++;}
static void request_face() {face_requested=true;}
static void settings_home() {}
static bool settings_typing() {return false;}
static bool settings_back() {return false;}
static void settings_scroll(int) {}
static bool settings_keyboard_changed() {return false;}
static bool s_image_shown;
enum muse_mode_t {MUSE_MODE_BOOT,MUSE_MODE_IDLE,MUSE_MODE_LISTENING,MUSE_MODE_THINKING,MUSE_MODE_SPEAKING};
struct {muse_mode_t mode;bool answering;} s_face;
static sticky_notes_model_t model;
}
extern "C" bool sticky_notes_view(unsigned selected,sticky_notes_view_t *view) {
    using namespace muse_eink;
    memset(view,0,sizeof(*view));view->count=model.count;view->selected=selected;
    for(unsigned i=0;i<model.count;i++) {
        snprintf(view->names[i],sizeof(view->names[i]),"%s",model.lists[i].name);
        snprintf(view->titles[i],sizeof(view->titles[i]),"%s",model.lists[i].title);
    }
    if(selected<model.count) view->list=model.lists[selected];
    view->revision=model.revision;view->synced_minute=480;return true;
}
extern "C" void sticky_notes_watch(void (*callback)(void)) {(void)callback;}
extern "C" bool sticky_notes_touch(const char *list,uint32_t id,bool clear) {
    using namespace muse_eink;
    cJSON *params=cJSON_CreateObject();cJSON_AddStringToObject(params,"list",list);
    if(!clear)cJSON_AddNumberToObject(params,"id",id);
    bool changed;
    cJSON *result=sticky_notes_apply(&model,clear?"notes.clear_done":"notes.toggle",params,0,&changed);
    bool ok=cJSON_IsTrue(cJSON_GetObjectItem(result,"ok"));cJSON_Delete(params);cJSON_Delete(result);return ok;
}
#include "notes_page.inc"
namespace muse_eink {
#include "notes_navigation.inc"
#define SWIPE_PX 80
#define LONG_PRESS_MS 600
#define pdTRUE 1
struct TouchEvent {int16_t x,y;bool down;int64_t time_us;uint32_t epoch;};
static int s_touch_q;
static std::deque<TouchEvent> events;
static int xQueueReceive(int,TouchEvent *e,int) {
    if(events.empty())return 0;*e=events.front();events.pop_front();return 1;
}
#include "notes_touch.inc"
struct Target final : DrawTarget {
    Size measureText(FontId,const char *s,TextStyle)const override{return {(int16_t)(strlen(s)*7),22};}
    int16_t lineHeight(FontId)const override{return 22;}
    void fill(Rect,Paint,uint8_t,uint8_t)override{}
    void stroke(Rect,Paint,uint8_t,uint8_t,uint8_t)override{}
    void line(Point,Point,uint8_t,Paint)override{}
    void triangle(Point,Point,Point,Paint)override{}
    void text(Rect,const char *,TextStyle)override{}
    void bitmap(Rect,BitmapRef,BitmapMode,Paint,Rotation)override{}
};
static void screen(Screen &s,void *) {if(s_notes)notes_screen(s);}
static void render() {g_app->render();}
static void tap(int16_t x,int16_t y) {
    InputSnapshot in;in.touchX=x;in.touchY=y;in.touchReleased=true;assert(g_app->route(in));render();
}
static void swipe(int dx,int dy=0) {
    uint32_t epoch=s_navigation_epoch.load();
    events.push_back({240,400,true,0,epoch});
    events.push_back({(int16_t)(240+dx),(int16_t)(400+dy),false,200000,epoch});
    handle_touch();render();
}
static void add_list(const char *name,unsigned count) {
    cJSON *params=cJSON_CreateObject(),*items=cJSON_CreateArray();
    cJSON_AddStringToObject(params,"list",name);cJSON_AddItemToObject(params,"items",items);
    for(unsigned i=0;i<count;i++)cJSON_AddItemToArray(items,cJSON_CreateString("An item"));
    bool changed;cJSON *result=sticky_notes_apply(&model,"notes.set",params,0,&changed);
    assert(changed);cJSON_Delete(result);cJSON_Delete(params);
}
static void test(const std::string &name) {
    Target target;DeviceContext device;device.width=480;device.height=800;device.hasTouch=true;
    App app(target,device);g_app=&app;app.setScreen(screen);notes_setup(app);
    sticky_notes_model_init(&model);add_list("shopping",20);add_list("chores",1);
    s_notes=true;s_face.mode=MUSE_MODE_IDLE;render();
    if(name=="toggle") {
        tap(100,160);assert(model.lists[0].items[0].done);
        tap(100,160);assert(!model.lists[0].items[0].done);
    } else if(name=="lists") {
        swipe(-100);assert(s_selected==1 && s_notes && !s_settings);
        swipe(100);assert(s_selected==0 && s_notes);
        tap(360,84);assert(s_selected==1);
        swipe(-100);assert(s_settings && !s_notes);
        swipe(100);assert(!s_settings && s_notes);
        swipe(100);assert(s_notes && s_selected==0);
        swipe(100);assert(!s_notes && s_face_active);
        swipe(-100);assert(s_notes);
        tap(420,24);assert(face_requested);
    } else if(name=="paging") {
        swipe(0,-100);assert(s_first==7);tap(100,160);assert(model.lists[0].items[7].done);
        tap(440,680);assert(s_first==14);tap(100,160);assert(model.lists[0].items[14].done);
        tap(60,680);assert(s_first==7);
    } else if(name=="clear") {
        tap(100,160);tap(100,234);assert(model.lists[0].count==20);
        uint32_t old_epoch=s_navigation_epoch.load();
        tap(240,726);assert(model.lists[0].count==18 && model.lists[0].items[0].id==3);
        events.push_back({100,160,true,0,old_epoch});
        events.push_back({100,160,false,100000,old_epoch});
        handle_touch();assert(!model.lists[0].items[0].done);
    } else {
        s_face.mode=MUSE_MODE_LISTENING;notes_voice_update(MUSE_MODE_IDLE);
        assert(!s_notes && s_return_notes);
        s_face.mode=MUSE_MODE_THINKING;s_face.answering=true;notes_voice_update(MUSE_MODE_LISTENING);
        assert(!s_notes && s_saw_reply);
        s_face.mode=MUSE_MODE_SPEAKING;notes_voice_update(MUSE_MODE_THINKING);assert(!s_notes);
        s_face.mode=MUSE_MODE_IDLE;s_face.answering=false;
        if(name=="reply_settings") {
            set_page(true);notes_voice_update(MUSE_MODE_SPEAKING);
            assert(s_settings && !s_notes && s_return_notes);
            set_page(false);
        }
        notes_voice_update(MUSE_MODE_SPEAKING);
        assert(s_notes && !s_return_notes && !s_face_active);
    }
}
}
int main(int argc,char **argv) {assert(argc==2);muse_eink::test(argv[1]);puts("ok");}
