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
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "kid_board.c"
#include "nvs.h"

static int64_t clock_epoch = 1730707200LL, uptime = 10000000;
static int clock_offset;
static gadget_clock_change_cb clock_callback;
static bool allowed = true, nvs_fail;
static unsigned posts, soft, gentle, writes, refreshes, rtc_writes;
static char summary[512], event_type[32];
static cJSON *last_result, *last_event;
static bool event_fail, show_fail, render_auto = true;
static epaper_cards_render_cb pending_render;
static void *pending_ctx;
static unsigned result_count;
static unsigned redraw_requests, current_requests, date_requests;
static int64_t wait_ticks;
static jmp_buf idle;
static uint8_t stored[16384];
static size_t stored_size;
static const int64_t DAY = 1730678400LL;

typedef struct {unsigned size, head, count; unsigned char data[8][256];} fake_queue_t;

int64_t esp_timer_get_time(void) {return uptime;}
int64_t gadget_clock_epoch(void) {return clock_epoch;}
int gadget_clock_offset_minutes(void) {return clock_offset;}
bool gadget_clock_valid(void) {return clock_epoch>=1704067200LL;}
bool gadget_clock_restore_utc(int64_t epoch) {clock_epoch=epoch;return true;}
void gadget_clock_set_change_cb(gadget_clock_change_cb cb) {clock_callback=cb;}
bool gadget_events_post(const char *source,const char *text,const cJSON *data) {
    assert(!strcmp(source,"Kid Board"));if(event_fail)return false;posts++;cJSON_Delete(last_event);last_event=cJSON_Duplicate(data,true);snprintf(summary,sizeof(summary),"%s",text);
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(data,"type");assert(cJSON_IsString(v));
    snprintf(event_type,sizeof(event_type),"%s",v->valuestring);return true;
}
bool gadget_events_post_seq(const char *source,const char *text,const cJSON *data,uint64_t *seq) {
    bool ok=gadget_events_post(source,text,data);if(ok&&seq)*seq=posts;return ok;
}
void kid_test_log(const char *tag,const char *format,...) {
    assert(!strcmp(tag,"link.kid"));char line[512];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
    assert(!strstr(line,"Mia")&&!strstr(line,"Grandpa")&&!strstr(line,"Homework"));
    if (!strncmp(line,"redraw request reason=",22)) {
        redraw_requests++;
        current_requests+=strstr(line,"reason=current ")!=NULL;
        date_requests+=strstr(line,"reason=date ")!=NULL;
    }
    printf("@kid %s\n",line);
}
static bool direct_event(const char *text,const cJSON *data) {return gadget_events_post("Kid Board",text,data);}
static void direct_chime(bool g) {if(g)gentle++;else soft++;}
static const kid_hooks_t hooks={direct_event,direct_chime};
bool reterminal_io_init(reterminal_ready_cb ready,reterminal_key_cb cb) {(void)ready;(void)cb;return true;}
void reterminal_io_chime(bool g) {direct_chime(g);}
bool reterminal_io_rtc_read(int64_t *out) {*out=DAY+8*3600;return true;}
bool reterminal_io_rtc_write(int64_t epoch) {assert(epoch==clock_epoch);rtc_writes++;return true;}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {return (void *)1;}
BaseType_t xSemaphoreTake(SemaphoreHandle_t h,TickType_t wait) {(void)h;(void)wait;return pdTRUE;}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {(void)h;return pdTRUE;}
QueueHandle_t xQueueCreate(unsigned length,unsigned size) {
    assert(length==8 && size<=256);fake_queue_t *q=calloc(1,sizeof(*q));assert(q);q->size=size;return q;
}
BaseType_t xQueueSend(QueueHandle_t handle,const void *item,TickType_t wait) {
    (void)wait;fake_queue_t *q=handle;if(q->count==8)return pdFALSE;
    memcpy(q->data[(q->head+q->count++)%8],item,q->size);return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t handle,void *item,TickType_t wait) {
    (void)wait;fake_queue_t *q=handle;if(!q->count)return pdFALSE;
    memcpy(item,q->data[q->head],q->size);q->head=(q->head+1)%8;q->count--;return pdTRUE;
}
unsigned uxQueueMessagesWaiting(QueueHandle_t handle) {return ((fake_queue_t *)handle)->count;}
BaseType_t xTaskCreate(TaskFunction_t task,const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out) {
    (void)task;(void)name;(void)stack;(void)arg;(void)priority;if(out)*out=(void *)1;return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) {(void)task;}
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t ticks) {(void)clear;wait_ticks=ticks;longjmp(idle,1);}
static void finish_render(void) {
    static uint8_t pixels[800*480/2];epaper_cards_canvas_t canvas;
    assert(epaper_cards_canvas(&canvas,pixels,sizeof(pixels),800,480,4));
    assert(pending_render);pending_render(&canvas,pending_ctx);pending_render=NULL;
}
bool epaper_cards_show(epaper_cards_render_cb cb,void *ctx) {
    if(show_fail)return false;
    assert(!pending_render);pending_render=cb;pending_ctx=ctx;refreshes++;if(render_auto)finish_render();return true;
}
void noise_ctrl_send_command_result(noise_ctrl_session_generation_t generation,const char *id,cJSON *result) {
    assert(generation==1 && !strcmp(id,"kid-1"));cJSON_Delete(last_result);last_result=result;result_count++;
}
esp_err_t nvs_open(const char *name,nvs_open_mode_t mode,nvs_handle_t *out) {(void)mode;assert(!strcmp(name,"kid_board"));*out=1;return nvs_fail?ESP_FAIL:ESP_OK;}
void nvs_close(nvs_handle_t h) {(void)h;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *bytes,size_t size) {
    (void)h;assert(!strcmp(key,"model") && size<=sizeof(stored));memcpy(stored,bytes,size);stored_size=size;writes++;return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *bytes,size_t *size) {
    (void)h;assert(!strcmp(key,"model"));if(!stored_size || *size<stored_size)return ESP_FAIL;
    memcpy(bytes,stored,stored_size);*size=stored_size;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {(void)h;return ESP_OK;}


static void apply(kid_model_t *m,const char *name,const char *json,int64_t now) {
    cJSON *p=cJSON_Parse(json);assert(p);bool changed;const char *error;
    bool ok=kid_model_command(m,name,p,now,0,&changed,&error);
    if(!ok)fprintf(stderr,"command: %s\n",error);
    assert(ok && !error);cJSON_Delete(p);
}
static const char *morning="{\"child\":\"Mia\",\"date\":\"2024-11-04\",\"items\":["
    "{\"id\":\"wake\",\"time\":\"07:00\",\"label\":\"Wake up\",\"icon\":\"wake\"},"
    "{\"id\":\"teeth\",\"time\":\"07:05\",\"label\":\"Teeth\",\"icon\":\"toothbrush\"},"
    "{\"id\":\"dress\",\"time\":\"07:10\",\"label\":\"Dress\",\"icon\":\"shirt\"},"
    "{\"id\":\"eat\",\"time\":\"07:20\",\"label\":\"Breakfast\",\"icon\":\"breakfast\"},"
    "{\"id\":\"school\",\"time\":\"07:40\",\"label\":\"School\",\"icon\":\"backpack\",\"who\":\"Dad\"}]}";
static const char *afternoon="{\"child\":\"Mia\",\"date\":\"2024-11-04\",\"items\":["
    "{\"id\":\"lunch\",\"time\":\"12:00\",\"label\":\"Lunch\",\"icon\":\"bowl\"},"
    "{\"id\":\"play\",\"time\":\"13:00\",\"label\":\"Play\",\"icon\":\"play\"},"
    "{\"id\":\"piano\",\"time\":\"16:00\",\"label\":\"Piano\",\"icon\":\"piano\",\"who\":\"Grandpa\"},"
    "{\"id\":\"pickup\",\"time\":\"17:00\",\"label\":\"Pick up\",\"icon\":\"car\",\"who\":\"Grandpa\"},"
    "{\"id\":\"hw\",\"time\":\"17:30\",\"label\":\"Homework\",\"icon\":\"homework\"},"
    "{\"id\":\"park\",\"time\":\"18:00\",\"label\":\"Park\",\"icon\":\"park\",\"who\":\"Mum\"},"
    "{\"id\":\"dinner\",\"time\":\"18:30\",\"label\":\"Dinner\",\"icon\":\"bowl\"},"
    "{\"id\":\"bath\",\"time\":\"19:00\",\"label\":\"Bath\",\"icon\":\"bath\"},"
    "{\"id\":\"read\",\"time\":\"19:30\",\"label\":\"Read\",\"icon\":\"book\"},"
    "{\"id\":\"bed\",\"time\":\"20:00\",\"label\":\"Sleep\",\"icon\":\"sleep\"}]}";
static const char *star_plan="{\"count\":3,\"goal\":5,\"reward_label\":\"Park picnic\",\"reward_icon\":\"gift\",\"note\":\"Proud of you, Mia! Love, Mum and Dad\"}";

static void validation(void) {
    kid_model_t m;kid_model_init(&m);
    const char *invalid[]={"null","[]","{}","{\"items\":{}}","{\"items\":[{}]}",
      "{\"items\":[],\"other\":true}","{\"items\":[],\"child\":3}","{\"items\":[],\"child\":\"\"}",
      "{\"items\":[],\"child\":\"line\\nline\"}","{\"items\":[],\"child\":\"Mia\",\"child\":\"Dad\"}",
      "{\"items\":[],\"date\":\"2025-02-29\"}","{\"items\":[],\"date\":\"2024-04-31\"}",
      "{\"items\":[],\"date\":\"2024-13-01\"}","{\"items\":[],\"date\":\"2024-01-00\"}",
      "{\"items\":[],\"date\":\"2100-01-01\"}","{\"items\":[],\"date\":\"2024-1-01\"}"};
    bool changed;const char *error;
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        kid_model_t before=m;cJSON *p=cJSON_Parse(invalid[i]);
        assert(!kid_model_command(&m,"kid.set_schedule",p,DAY,0,&changed,&error));assert(error&&!changed&&!memcmp(&m,&before,sizeof(m)));cJSON_Delete(p);
    }
    const char *bad_stars[]={"{}","{\"count\":-1,\"goal\":5}","{\"count\":1.5,\"goal\":5}","{\"count\":1,\"goal\":0}",
      "{\"count\":1,\"goal\":21}","{\"count\":1000,\"goal\":5}","{\"count\":true,\"goal\":5}",
      "{\"count\":1,\"goal\":5,\"note\":\"two\\nlines\"}","{\"count\":1,\"goal\":5,\"reward_icon\":\"bad icon\"}"};
    for(unsigned i=0;i<sizeof(bad_stars)/sizeof(bad_stars[0]);i++){cJSON *p=cJSON_Parse(bad_stars[i]);assert(!kid_model_command(&m,"kid.set_stars",p,DAY,0,&changed,&error));cJSON_Delete(p);}
    apply(&m,"kid.set_schedule",afternoon,DAY);
    cJSON *p=cJSON_Parse(afternoon),*items=cJSON_GetObjectItem(p,"items");
    cJSON_AddItemToArray(items,cJSON_Duplicate(cJSON_GetArrayItem(items,0),true));
    assert(!kid_model_command(&m,"kid.set_schedule",p,DAY,0,&changed,&error));cJSON_DeleteItemFromArray(items,10);
    const char *fields_to_test[]={"id","time","label","icon","who"};
    const char *values[]={"bad id","24:00","","bad icon","line\nline"};
    for(int i=0;i<5;i++) {
        cJSON *q=cJSON_Duplicate(p,true),*v=cJSON_GetArrayItem(cJSON_GetObjectItem(q,"items"),0);
        cJSON_DeleteItemFromObjectCaseSensitive(v,fields_to_test[i]);cJSON_AddStringToObject(v,fields_to_test[i],values[i]);
        assert(!kid_model_command(&m,"kid.set_schedule",q,DAY,0,&changed,&error));cJSON_Delete(q);
    }
    cJSON *q=cJSON_Duplicate(p,true),*v=cJSON_GetArrayItem(cJSON_GetObjectItem(q,"items"),0);
    cJSON_ReplaceItemInObjectCaseSensitive(v,"time",cJSON_CreateString("7:00"));assert(!kid_model_command(&m,"kid.set_schedule",q,DAY,0,&changed,&error));cJSON_Delete(q);
    q=cJSON_Duplicate(p,true);items=cJSON_GetObjectItem(q,"items");
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetArrayItem(items,1),"id",cJSON_CreateString("lunch"));
    assert(!kid_model_command(&m,"kid.set_schedule",q,DAY,0,&changed,&error));cJSON_Delete(q);
    q=cJSON_Duplicate(p,true);items=cJSON_GetObjectItem(q,"items");char long_text[100];memset(long_text,'x',99);long_text[99]=0;
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetArrayItem(items,0),"label",cJSON_CreateString(long_text));
    assert(!kid_model_command(&m,"kid.set_schedule",q,DAY,0,&changed,&error));cJSON_Delete(q);
    assert(kid_model_command(&m,"kid.set_schedule",p,DAY,0,&changed,&error)&&!changed);cJSON_Delete(p);
    p=cJSON_Parse("{\"id\":\"missing\",\"done\":true}");assert(!kid_model_command(&m,"kid.mark",p,DAY,0,&changed,&error));cJSON_Delete(p);
    p=cJSON_Parse("{\"id\":\"lunch\",\"done\":1}");assert(!kid_model_command(&m,"kid.mark",p,DAY,0,&changed,&error));cJSON_Delete(p);
    p=cJSON_Parse("{\"extra\":1}");assert(!kid_model_command(&m,"kid.state",p,DAY,0,&changed,&error));cJSON_Delete(p);
    p=cJSON_Parse("{\"items\":[]}");assert(!kid_model_command(&m,"kid.set_schedule",p,0,0,&changed,&error));cJSON_Delete(p);
    apply(&m,"kid.set_schedule","{\"date\":\"2024-02-29\",\"items\":[]}",0);
    apply(&m,"kid.set_schedule","{\"items\":[{\"id\":\"x\",\"time\":\"23:59\",\"label\":\"Mystery\",\"icon\":\"unknown\"}]}",DAY);
    assert(m.item_count==1&&!strcmp(m.date,"2024-11-04"));
}

static void selection(void) {
    kid_model_t m;kid_model_init(&m);apply(&m,"kid.set_schedule",morning,DAY);
    assert(kid_model_current(&m,0,0)==-1);assert(kid_model_current(&m,DAY+6*3600,0)==0);
    assert(kid_model_next_change(&m,0,0)==0);
    assert(kid_model_next_change(&m,DAY+6*3600,0)==DAY+7*3600+5*60);
    assert(kid_model_next_change(&m,DAY-3600,480)==DAY-3600+5*60);
    assert(kid_model_current(&m,DAY+9*3600,0)==4);
    assert(kid_model_next_change(&m,DAY+9*3600,0)==DAY+86400);
    apply(&m,"kid.mark","{\"id\":\"wake\",\"done\":true}",DAY+7*3600);
    assert(kid_model_current(&m,DAY+7*3600,0)==1);
    assert(m.items[0].done_at==DAY+7*3600);
    apply(&m,"kid.set_schedule",morning,DAY+8*3600);assert(m.items[0].done);
    assert(!kid_model_rollover(&m,DAY+23*3600+3599,0));
    assert(kid_model_rollover(&m,DAY+86400,0));assert(!m.items[0].done&&!m.items[0].done_at&&!strcmp(m.date,"2024-11-05"));
    assert(!kid_model_rollover(&m,DAY,0));assert(!kid_model_rollover(&m,0,0));
    apply(&m,"kid.mark","{\"id\":\"wake\",\"done\":true}",DAY+86400);
    apply(&m,"kid.set_schedule",morning,DAY);assert(!m.items[0].done);
    apply(&m,"kid.set_schedule","{\"date\":\"2024-11-05\",\"items\":[{\"id\":\"a\",\"time\":\"07:00\",\"label\":\"Wake\",\"icon\":\"wake\"}]}",DAY);
    assert(kid_model_current(&m,DAY+23*3600,0)==-1);assert(!kid_model_rollover(&m,DAY+23*3600,0));
    assert(kid_model_next_change(&m,DAY+23*3600,0)==DAY+86400);
    assert(kid_model_current(&m,DAY+86400+7*3600,0)==0);
    assert(kid_model_rollover(&m,DAY+86400+20*3600,480));assert(!strcmp(m.date,"2024-11-06"));
    apply(&m,"kid.set_schedule","{\"items\":[{\"id\":\"late\",\"time\":\"09:00\",\"label\":\"Late\",\"icon\":\"play\"},{\"id\":\"a\",\"time\":\"07:00\",\"label\":\"First\",\"icon\":\"wake\"},{\"id\":\"b\",\"time\":\"07:00\",\"label\":\"Second\",\"icon\":\"shirt\"}]}",DAY);
    assert(!strcmp(m.items[0].id,"a")&&!strcmp(m.items[1].id,"b"));
    assert(kid_model_current(&m,DAY+10*3600,0)==2);
    apply(&m,"kid.mark","{\"id\":\"a\",\"done\":true}",DAY);assert(kid_model_current(&m,DAY+7*3600,0)==1);
    apply(&m,"kid.mark","{\"id\":\"b\",\"done\":true}",DAY);assert(kid_model_current(&m,DAY+10*3600,0)==2);
    apply(&m,"kid.mark","{\"id\":\"late\",\"done\":true}",DAY);assert(kid_model_current(&m,DAY+10*3600,0)==-1);
    assert(kid_model_next_change(&m,DAY+10*3600,0)==DAY+86400);
    apply(&m,"kid.mark","{\"id\":\"b\",\"done\":false}",DAY);assert(!m.items[1].done_at);
}

static void keys(void) {
    kid_model_t m;kid_model_init(&m);apply(&m,"kid.set_schedule",morning,DAY);apply(&m,"kid.set_stars",star_plan,DAY);
    event_fail=true;assert(!kid_model_green(&m,DAY+7*3600,0,&hooks));assert(!m.items[0].done&&!posts&&!soft);
    event_fail=false;assert(kid_model_green(&m,DAY+7*3600,0,&hooks));assert(posts==1&&soft==1&&m.items[0].done&&m.count==3);
    assert(!strcmp(event_type,"kid.done")&&!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(last_event,"id")),"wake"));
    assert(cJSON_GetObjectItem(last_event,"at")->valuedouble==DAY+7*3600&&strstr(summary,"Mia confirmed"));
    apply(&m,"kid.set_stars","{\"count\":5,\"goal\":5}",DAY);cJSON *state=kid_model_json(&m,DAY,0);
    cJSON *stars=cJSON_GetObjectItem(state,"stars");assert(cJSON_IsTrue(cJSON_GetObjectItem(stars,"goal_reached")));
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(stars,"reward_label")),"Park picnic"));cJSON_Delete(state);
    apply(&m,"kid.set_stars","{\"count\":6,\"goal\":5}",DAY);assert(m.count==6);
    for(int i=1;i<5;i++)assert(kid_model_green(&m,DAY+7*3600+i,0,&hooks));
    assert(!kid_model_green(&m,DAY+7*3600+6,0,&hooks)&&posts==5&&m.count==6);
    apply(&m,"kid.set_schedule",afternoon,DAY);
    assert(kid_model_green(&m,DAY+16*3600,0,&hooks));assert(m.items[2].done&&!m.items[0].done);
}

static void persistence(void) {
    kid_model_t m,out;kid_model_init(&m);apply(&m,"kid.set_schedule",morning,DAY);apply(&m,"kid.set_stars",star_plan,DAY);
    assert(kid_model_green(&m,DAY+7*3600,0,&hooks));assert(kid_store_save(&m));kid_model_init(&out);
    assert(kid_store_load(&out)&&!memcmp(&m,&out,sizeof(m)));assert(out.items[0].done_at==DAY+7*3600&&out.count==3);
    stored[stored_size-1]^=1;assert(!kid_store_load(&out));stored[stored_size-1]^=1;
    stored_size--;assert(!kid_store_load(&out));nvs_fail=true;assert(!kid_store_save(&m));
}

static void refresh(void) {
    kid_refresh_t r={0};assert(kid_refresh_wait(&r,0)==-1);kid_refresh_request(&r);assert(kid_refresh_wait(&r,0)==0);
    kid_refresh_started(&r,0);kid_refresh_request(&r);assert(kid_refresh_wait(&r,1)==KID_REFRESH_US-1);
    for(int i=0;i<10;i++)kid_refresh_request(&r);
    assert(kid_refresh_wait(&r,KID_REFRESH_US)==0);kid_refresh_started(&r,KID_REFRESH_US);
    assert(kid_refresh_wait(&r,KID_REFRESH_US)==-1);kid_refresh_request(&r);assert(kid_refresh_wait(&r,10*KID_REFRESH_US)==0);
}

static bool ready(void) {return allowed;}
static void pump(void) {if(!setjmp(idle))kid_task(NULL);}
static void command(const char *name,const char *json) {
    cJSON *p=cJSON_Parse(json);unsigned before=writes, replies=result_count;
    cJSON *async=kid_board_command(name,p,"kid-1",1);cJSON_Delete(p);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(async,"_async"))&&writes==before);cJSON_Delete(async);pump();assert(last_result&&result_count==replies+1);
}
static bool result_ok(void) {return cJSON_IsTrue(cJSON_GetObjectItem(last_result,"ok"));}
static void controller(void) {
    clock_epoch=DAY+7*3600;assert(kid_board_init(ready));pump();assert(rtc_writes==1&&refreshes==0);
    command("kid.set_schedule",morning);assert(result_ok()&&refreshes==1&&writes==1);pump();
    unsigned before=writes;command("kid.set_schedule",morning);assert(result_ok()&&writes==before&&refreshes==1);
    command("kid.mark","{\"id\":\"wake\",\"done\":true}");assert(result_ok()&&refreshes==1);
    before=writes;command("kid.mark","{\"id\":\"wake\",\"done\":true}");assert(result_ok()&&writes==before);
    command("kid.set_stars",star_plan);assert(result_ok()&&refreshes==1);
    uptime+=KID_REFRESH_US;clock_epoch+=60;pump();assert(refreshes==2);pump();
    allowed=false;assert(!kid_board_key(RETERMINAL_GREEN));allowed=true;assert(!kid_board_key(RETERMINAL_LEFT));
    assert(kid_board_key(RETERMINAL_GREEN));pump();assert(s_model->items[1].done&&posts==1&&soft==1);
    assert(kid_board_key(RETERMINAL_GREEN));pump();assert(posts==1);
    uptime+=2000000;event_fail=true;assert(kid_board_key(RETERMINAL_GREEN));pump();assert(posts==1&&!s_model->items[2].done);event_fail=false;
    nvs_fail=true;assert(kid_board_key(RETERMINAL_GREEN));pump();assert(posts==2&&!s_storage_ok&&s_model->items[2].done);
    nvs_fail=false;uptime+=KID_REFRESH_US;clock_epoch+=60;pump();assert(s_storage_ok);
    kid_model_t saved;assert(kid_store_load(&saved)&&saved.items[2].done);
    command("kid.state","{}");assert(result_ok());cJSON *payload=cJSON_GetObjectItem(last_result,"payload");assert(cJSON_IsTrue(cJSON_GetObjectItem(payload,"clock_valid")));
    command("kid.state","{\"extra\":1}");assert(!result_ok());
    nvs_fail=true;int count=s_model->count;command("kid.set_stars","{\"count\":8,\"goal\":5}");assert(!result_ok()&&s_model->count==count);nvs_fail=false;
    command("kid.set_schedule","{\"child\":\"Changed\",\"items\":[{}]}");assert(!result_ok()&&!strcmp(s_model->child,"Mia"));
    for(int i=0;i<8;i++){cJSON *p=cJSON_CreateObject(),*result=kid_board_command("kid.state",p,"kid-1",1);cJSON_Delete(p);assert(cJSON_IsTrue(cJSON_GetObjectItem(result,"_async")));cJSON_Delete(result);}
    cJSON *p=cJSON_CreateObject(),*busy=kid_board_command("kid.state",p,"kid-1",1);assert(cJSON_IsFalse(cJSON_GetObjectItem(busy,"ok")));cJSON_Delete(busy);cJSON_Delete(p);pump();
    pump();uptime+=KID_REFRESH_US;clock_epoch+=60;render_auto=false;
    command("kid.set_stars","{\"count\":4,\"goal\":5}");assert(pending_render);unsigned draws=refreshes;
    command("kid.set_stars","{\"count\":5,\"goal\":5}");uptime+=2*KID_REFRESH_US;clock_epoch+=120;pump();assert(refreshes==draws);
    finish_render();pump();uptime+=KID_REFRESH_US-1;pump();assert(refreshes==draws);
    assert(!s_refresh.dirty);
    command("kid.set_stars","{\"count\":6,\"goal\":5}");
    uptime++;show_fail=true;pump();assert(refreshes==draws&&!atomic_load(&s_render_pending));show_fail=false;render_auto=true;pump();assert(refreshes==draws+1);
    clock_epoch=DAY+86400;pump();assert(!s_model->items[0].done&&!strcmp(s_model->date,"2024-11-05"));
    assert(wait_ticks>0);
}

// Simulates an hour through actual worker sleeps and independent clock notices.
// 通过实际后台休眠截止点及独立时钟通知模拟一小时。
static void hour(void) {
    clock_epoch=DAY+7*3600;assert(kid_board_init(ready));pump();
    cJSON *p=cJSON_CreateObject(),*items=cJSON_AddArrayToObject(p,"items");
    cJSON_AddStringToObject(p,"date","2024-11-04");
    for(int i=0;i<7;i++) {
        char id[8],at[6];snprintf(id,sizeof(id),"step%d",i);snprintf(at,sizeof(at),"%02d:%02d",7+i/6,i%6*10);
        cJSON *v=cJSON_CreateObject();cJSON_AddStringToObject(v,"id",id);cJSON_AddStringToObject(v,"time",at);
        cJSON_AddStringToObject(v,"label","Step");cJSON_AddStringToObject(v,"icon","play");cJSON_AddItemToArray(items,v);
    }
    char *json=cJSON_PrintUnformatted(p);assert(json);command("kid.set_schedule",json);free(json);cJSON_Delete(p);pump();
    assert(redraw_requests==1&&refreshes==1&&wait_ticks==600*configTICK_RATE_HZ);
    for(int second=1;second<=3600;second++) {
        clock_epoch++;uptime+=1000000;
        if(second%60)continue;
        unsigned before=redraw_requests,draws=refreshes;
        assert(clock_callback);clock_callback();pump();pump();
        if(second%600)assert(redraw_requests==before&&refreshes==draws);
        else assert(redraw_requests==before+1&&refreshes==draws+1);
        assert(kid_model_current(s_model,clock_epoch,0)==second/600);
        int64_t until=second<3600?600-second%600:16*3600;
        assert(wait_ticks==until*configTICK_RATE_HZ);
    }
    assert(redraw_requests==7&&refreshes==7&&current_requests==6&&date_requests==0&&writes==1);
}

static void minute(void) {
    clock_epoch=DAY+7*3600;assert(kid_board_init(ready));pump();command("kid.set_stars",star_plan);pump();
    assert(redraw_requests==1&&refreshes==1&&wait_ticks==17*3600*configTICK_RATE_HZ);
    static uint8_t before[800*480/2],after[800*480/2];epaper_cards_canvas_t c;
    assert(epaper_cards_canvas(&c,before,sizeof(before),800,480,4));kid_model_render(&c,s_model,clock_epoch,0);
    for(int i=0;i<60;i++){clock_epoch+=60;uptime+=KID_REFRESH_US;clock_callback();pump();}
    assert(epaper_cards_canvas(&c,after,sizeof(after),800,480,4));kid_model_render(&c,s_model,clock_epoch,0);
    assert(!memcmp(before,after,sizeof(before))&&redraw_requests==1&&refreshes==1&&writes==1);
    command("kid.set_stars",star_plan);command("kid.state","{}");assert(redraw_requests==1&&refreshes==1&&writes==1);
}

static void midnight(void) {
    kid_model_t m;kid_model_init(&m);apply(&m,"kid.set_schedule",morning,DAY);
    for(unsigned i=0;i<m.item_count;i++){m.items[i].done=true;m.items[i].done_at=DAY+8*3600;}
    assert(kid_store_save(&m));clock_epoch=DAY+86400-60;
    assert(kid_board_init(ready));pump();pump();assert(redraw_requests==1&&refreshes==1&&wait_ticks==60*configTICK_RATE_HZ);
    clock_epoch+=60;uptime+=KID_REFRESH_US;pump();pump();
    assert(redraw_requests==2&&refreshes==2&&date_requests==1&&!strcmp(s_model->date,"2024-11-05"));
    for(unsigned i=0;i<s_model->item_count;i++)assert(!s_model->items[i].done);
    assert(wait_ticks==425*60*configTICK_RATE_HZ);
    clock_epoch+=60;uptime+=KID_REFRESH_US;pump();assert(redraw_requests==2&&refreshes==2);
}

static void assets(void) {
    assert(card_font_find(20)&&card_font_find(28)&&card_font_find(40));assert(!card_font_find(60)&&!card_font_find(96));
    const char *names[]={"wake","sun","toothbrush","shirt","breakfast","bowl","backpack","school","homework","book","swim","piano","music","bath","bed","sleep","play","park","car","pickup","star","gift","reward","check_circle"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++){assert(card_icon_find(names[i],64));assert(!card_icon_find(names[i],112));}
    assert(!card_icon_find("unknown",64));assert(!card_icon_find("medication",64));
    kid_model_t m;kid_model_init(&m);
    apply(&m,"kid.set_schedule","{\"items\":[{\"id\":\"x\",\"time\":\"07:00\",\"label\":\"Mystery\",\"icon\":\"unknown\"}]}",DAY);
    apply(&m,"kid.set_stars","{\"count\":20,\"goal\":20,\"reward_icon\":\"unknown\"}",DAY);
    static uint8_t pixels[800*480/2];epaper_cards_canvas_t c;assert(epaper_cards_canvas(&c,pixels,sizeof(pixels),800,480,4));
    kid_model_render(&c,&m,DAY+7*3600,0);
    unsigned letters=0;for(int y=115;y<180;y++)for(int x=20;x<152;x++)letters+=epaper_cards_get(&c,x,y)==CARD_BLACK;
    assert(letters>150);
}
static void preview(const char *name,const kid_model_t *m,int64_t now) {
    const char *dir=getenv("CARD_PREVIEW_DIR");if(!dir)return;mkdir(dir,0755);
    static uint8_t pixels[800*480/2];epaper_cards_canvas_t c;assert(epaper_cards_canvas(&c,pixels,sizeof(pixels),800,480,4));
    kid_model_render(&c,m,now,0);char path[4096];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);FILE *file=fopen(path,"wb");assert(file);
    fputs("P6\n800 480\n255\n",file);
    static const unsigned char rgb[7][3]={{0,0,0},{255,255,255},{255,255,0},{255,0,0},{0,0,0},{0,0,255},{0,255,0}};
    for(int y=0;y<480;y++)for(int x=0;x<800;x++){card_color_t color=epaper_cards_get(&c,x,y);assert(color!=4&&color<=6);assert(fwrite(rgb[color],1,3,file)==3);}fclose(file);
}
static void previews(void) {
    kid_model_t m;kid_model_init(&m);apply(&m,"kid.set_schedule",morning,DAY);apply(&m,"kid.set_stars",star_plan,DAY);preview("kid_morning",&m,DAY+7*3600);
    assert(kid_model_green(&m,DAY+7*3600+1,0,&hooks));preview("kid_done",&m,DAY+7*3600+1);
    apply(&m,"kid.set_schedule",afternoon,DAY);
    apply(&m,"kid.mark","{\"id\":\"lunch\",\"done\":true}",DAY+12*3600);
    apply(&m,"kid.mark","{\"id\":\"play\",\"done\":true}",DAY+13*3600);
    preview("kid_afternoon_changed",&m,DAY+16*3600);
    apply(&m,"kid.set_stars","{\"count\":5,\"goal\":5}",DAY);preview("kid_stars_goal",&m,DAY+17*3600);
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"validation"))validation();else if(!strcmp(argv[1],"selection"))selection();else if(!strcmp(argv[1],"keys"))keys();
    else if(!strcmp(argv[1],"store"))persistence();else if(!strcmp(argv[1],"refresh"))refresh();else if(!strcmp(argv[1],"controller"))controller();
    else if(!strcmp(argv[1],"hour"))hour();else if(!strcmp(argv[1],"minute"))minute();else if(!strcmp(argv[1],"midnight"))midnight();
    else if(!strcmp(argv[1],"preview"))previews();else if(!strcmp(argv[1],"assets"))assets();else assert(false);
    cJSON_Delete(last_result);cJSON_Delete(last_event);puts("ok");return 0;
}
