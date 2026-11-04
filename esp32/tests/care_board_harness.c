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
#include "care_board.c"
#include "nvs.h"
#include "rtc_protocol.inc"

static int64_t clock_epoch = 1730707200LL, uptime = 10000000;
static int clock_offset;
static gadget_clock_change_cb clock_callback;
static bool allowed = true, nvs_fail, clock_fail;
static unsigned posts, soft, gentle, silenced, writes, refreshes, rtc_writes;
static bool led_due;
static char summary[512], event_type[32];
static cJSON *last_result;
static unsigned result_count;
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
bool gadget_clock_set(int64_t epoch,int offset) {if(clock_fail)return false;clock_epoch=epoch;clock_offset=offset;if(clock_callback)clock_callback();return true;}
bool gadget_clock_restore_utc(int64_t epoch) {clock_epoch=epoch;return true;}
void gadget_clock_set_change_cb(gadget_clock_change_cb cb) {clock_callback=cb;}
bool gadget_events_post(const char *source,const char *text,const cJSON *data) {
    assert(!strcmp(source,"Care Board"));posts++;snprintf(summary,sizeof(summary),"%s",text);
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(data,"type");assert(cJSON_IsString(v));
    snprintf(event_type,sizeof(event_type),"%s",v->valuestring);return true;
}
bool gadget_events_post_seq(const char *source,const char *text,const cJSON *data,uint64_t *seq) {
    bool ok=gadget_events_post(source,text,data);if(ok&&seq)*seq=posts;return ok;
}
void care_test_log(const char *tag,const char *format,...) {
    assert(!strcmp(tag,"link.care"));char line[512];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
    assert(!strstr(line,"Blood pressure pill")&&!strstr(line,"See you")&&!strstr(line,"Home at seven"));
    printf("@care %s\n",line);
}
static bool direct_event(const char *text,const cJSON *data) {return gadget_events_post("Care Board",text,data);}
static void direct_chime(bool g) {if(g)gentle++;else soft++;}
static const care_hooks_t hooks={direct_event,direct_chime};
bool reterminal_io_init(reterminal_ready_cb ready,reterminal_key_cb cb) {(void)ready;(void)cb;return true;}
void reterminal_io_due(bool due) {led_due=due;}
void reterminal_io_chime(bool g) {direct_chime(g);}
void reterminal_io_silence(void) {silenced++;}
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
void led_status_show_animation(void) {refreshes++;}
bool epaper_cards_show(epaper_cards_render_cb cb,void *ctx) {(void)cb;(void)ctx;refreshes++;return true;}
cJSON *reterminal_sht4x_command(void) {return cJSON_Parse("{\"ok\":true,\"payload\":{\"temperature\":{\"value\":22.5},\"humidity\":{\"value\":48}}}");}
void noise_ctrl_send_command_result(noise_ctrl_session_generation_t generation,const char *id,cJSON *result) {
    assert(generation==1 && !strcmp(id,"care-1"));cJSON_Delete(last_result);last_result=result;result_count++;
}
esp_err_t nvs_open(const char *name,nvs_open_mode_t mode,nvs_handle_t *out) {(void)mode;assert(!strcmp(name,"care_board"));*out=1;return nvs_fail?ESP_FAIL:ESP_OK;}
void nvs_close(nvs_handle_t h) {(void)h;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *bytes,size_t size) {
    (void)h;assert(!strcmp(key,"model") && size<=sizeof(stored));memcpy(stored,bytes,size);stored_size=size;writes++;return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *bytes,size_t *size) {
    (void)h;assert(!strcmp(key,"model"));if(!stored_size || *size<stored_size)return ESP_FAIL;
    memcpy(bytes,stored,stored_size);*size=stored_size;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {(void)h;return ESP_OK;}

static void patch(care_model_t *m,const char *json,int64_t now) {
    care_model_t candidate=*m;bool message;const char *error;cJSON *p=cJSON_Parse(json);assert(p);
    bool ok=care_model_patch(&candidate,p,now,0,&message,&error);
    if(!ok)fprintf(stderr,"patch: %s\n",error);
    assert(ok);*m=candidate;cJSON_Delete(p);
}
static void validation(void) {
    care_model_t m;care_model_init(&m);
    const char *invalid[]={"null","[]","{\"other\":1}","{\"person\":2}","{\"family\":\"\"}",
      "{\"now_epoch\":1730707200}","{\"now_epoch\":1,\"utc_offset_min\":0}",
      "{\"now_epoch\":1730707200,\"utc_offset_min\":841}","{\"weather\":{\"text\":\"rain\",\"icon\":\"absent\",\"temp\":22}}",
      "{\"messages\":[{}, {}, {}, {}]}","{\"messages\":[{\"from\":\"Mia\",\"text\":true}]}",
      "{\"messages\":[],\"add_message\":{\"from\":\"Mia\",\"text\":\"hi\"}}",
      "{\"reminders\":[{\"id\":\"a\",\"time\":\"24:00\",\"label\":\"pill\",\"repeat\":\"daily\"}]}",
      "{\"reminders\":[{\"id\":\"a\",\"time\":\"8:00\",\"label\":\"pill\",\"repeat\":\"daily\"}]}",
      "{\"reminders\":[{\"id\":\"a\",\"time\":\"08:00\",\"label\":\"pill\",\"repeat\":\"weekly\"}]}",
      "{\"reminders\":[{\"id\":\"a\",\"time\":\"08:00\",\"label\":\"pill\",\"repeat\":\"daily\",\"escalate_min\":0}]}",
      "{\"person\":\"Dad\",\"person\":\"Mum\"}","{\"person\":\"line\\nline\"}"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        care_model_t candidate=m;bool added;const char *err;cJSON *p=cJSON_Parse(invalid[i]);
        assert(!care_model_patch(&candidate,p,DAY+7*3600,0,&added,&err) && err);cJSON_Delete(p);
    }
    cJSON *p=cJSON_CreateObject();char long_text[CARE_TEXT+1];memset(long_text,'x',sizeof(long_text)-1);long_text[CARE_TEXT]=0;
    cJSON *add=cJSON_AddObjectToObject(p,"add_message");cJSON_AddStringToObject(add,"from","Mia");cJSON_AddStringToObject(add,"text",long_text);
    bool added;const char *err;assert(!care_model_patch(&m,p,DAY,0,&added,&err));cJSON_Delete(p);
    p=cJSON_Parse("{\"reminders\":[]}");cJSON *items=cJSON_GetObjectItem(p,"reminders");
    for(int i=0;i<9;i++){char id[8];snprintf(id,sizeof(id),"r%d",i);cJSON *q=cJSON_CreateObject();cJSON_AddStringToObject(q,"id",id);cJSON_AddStringToObject(q,"time","08:00");cJSON_AddStringToObject(q,"label","Pill");cJSON_AddStringToObject(q,"repeat","daily");cJSON_AddItemToArray(items,q);}
    assert(!care_model_patch(&m,p,DAY,0,&added,&err));cJSON_Delete(p);
    patch(&m,"{\"person\":\"Dad\",\"family\":\"Mia\",\"weather\":{\"text\":\"Rain later\",\"icon\":\"rainy\",\"temp\":19.5},\"messages\":[{\"from\":\"Mia\",\"text\":\"Later\",\"time\":100},{\"from\":\"Mia\",\"text\":\"Newest\",\"time\":200}]}",DAY);
    assert(m.weather_temp==19.5 && !strcmp(m.messages[0].text,"Newest"));
    for(int i=0;i<5;i++)patch(&m,"{\"add_message\":{\"from\":\"Mia\",\"text\":\"See you at seven\"}}",DAY+i);
    assert(m.message_count==3 && !m.messages[0].read);
}
static const char *reminders="{\"family\":\"Mia\",\"reminders\":[{\"id\":\"pill\",\"time\":\"08:00\",\"label\":\"Blood pressure pill\",\"icon\":\"medication\",\"repeat\":\"daily\"}]}";
static void engine(void) {
    care_model_t m;care_model_init(&m);patch(&m,reminders,DAY+7*3600);
    bool changed;int64_t next=care_model_step(&m,DAY+7*3600,0,&hooks,&changed);assert(next==DAY+8*3600);
    care_model_step(&m,DAY+8*3600,0,&hooks,&changed);assert(m.reminders[0].status==CARE_DUE && gentle==1 && posts==1);
    assert(!strcmp(event_type,"due"));assert(strstr(summary,"is due now (08:00)"));
    care_model_step(&m,DAY+8*3600+300,0,&hooks,&changed);assert(gentle==2);
    care_model_step(&m,DAY+8*3600+600,0,&hooks,&changed);assert(gentle==3);
    care_model_step(&m,DAY+8*3600+900,0,&hooks,&changed);assert(gentle==3);
    care_model_step(&m,DAY+8*3600+1800,0,&hooks,&changed);assert(m.reminders[0].status==CARE_MISSED && posts==2 && gentle==3);
    assert(!strcmp(event_type,"missed") && strstr(summary,"30 minutes after 08:00"));
    assert(care_model_key(&m,CARE_GREEN,DAY+8*3600+1801,0,10000000,&hooks));
    assert(m.reminders[0].status==CARE_DONE && m.reminders[0].done_at==DAY+8*3600+1801 && !strcmp(event_type,"ack"));
    assert(strstr(m.footer,"Told Mia at 08:30"));
    patch(&m,reminders,DAY+9*3600);assert(m.reminders[0].status==CARE_DONE);
    care_model_step(&m,DAY+86400+1,0,&hooks,&changed);assert(m.reminders[0].status==CARE_PENDING);
    assert(m.reminders[0].next_due==DAY+86400+8*3600);
    patch(&m,reminders,DAY+86400+9*3600);assert(m.reminders[0].status==CARE_PENDING);
    care_model_init(&m);patch(&m,reminders,DAY+9*3600);
    assert(m.reminders[0].next_due==DAY+86400+8*3600);
    care_model_step(&m,DAY+9*3600,0,&hooks,&changed);assert(m.reminders[0].status==CARE_PENDING);
    care_model_init(&m);patch(&m,"{\"reminders\":[{\"id\":\"once\",\"time\":\"08:00\",\"label\":\"Carer visit\",\"repeat\":\"once\"}]}",DAY+7*3600);
    care_model_step(&m,DAY+8*3600,0,&hooks,&changed);assert(care_model_key(&m,CARE_GREEN,DAY+8*3600+10,0,10000000,&hooks));
    care_model_step(&m,DAY+86400,0,&hooks,&changed);assert(!m.reminder_count);
    care_model_init(&m);patch(&m,"{\"reminders\":[{\"id\":\"midnight\",\"time\":\"00:00\",\"label\":\"Water\",\"repeat\":\"daily\"}]}",DAY+12*3600);
    care_model_step(&m,DAY+86400+1,0,&hooks,&changed);assert(m.reminders[0].status==CARE_DUE);
    care_model_init(&m);patch(&m,"{\"reminders\":[{\"id\":\"midnight\",\"time\":\"00:00\",\"label\":\"Water\",\"repeat\":\"once\"}]}",DAY+12*3600);
    care_model_step(&m,DAY+86400+2,0,&hooks,&changed);
    assert(m.reminders[0].status==CARE_DUE && m.reminders[0].due_at==DAY+86400);
    unsigned prior=posts;care_model_step(&m,0,0,&hooks,&changed);assert(posts==prior && !changed);
    care_model_init(&m);cJSON *zone_patch=cJSON_Parse(reminders);bool added;const char *error;
    assert(care_model_patch(&m,zone_patch,DAY-3600,480,&added,&error));cJSON_Delete(zone_patch);
    assert(m.reminders[0].next_due==DAY);
    care_model_step(&m,DAY,480,&hooks,&changed);assert(m.reminders[0].status==CARE_DUE);
    care_model_step(&m,DAY+16*3600,480,&hooks,&changed);assert(m.reminders[0].status==CARE_PENDING && m.reminders[0].next_due==DAY+86400);
    care_model_init(&m);patch(&m,reminders,DAY-3600);
    care_model_step(&m,DAY-3600,480,&hooks,&changed);
    assert(changed && m.reminders[0].next_due==DAY && m.schedule_offset==480);
    care_model_step(&m,DAY,480,&hooks,&changed);assert(m.reminders[0].status==CARE_DUE);
    assert(care_model_key(&m,CARE_GREEN,DAY+10,480,10000000,&hooks));
    care_model_step(&m,DAY+20,420,&hooks,&changed);assert(m.reminders[0].status==CARE_DONE);
    care_model_init(&m);patch(&m,reminders,DAY+7*3600);
    patch(&m,"{\"reminders\":[{\"id\":\"new_plan\",\"time\":\"08:00\",\"label\":\"Water\",\"repeat\":\"daily\"}]}",DAY+86400+9*3600);
    care_model_step(&m,DAY+86400+9*3600,0,&hooks,&changed);
    assert(m.reminders[0].status==CARE_PENDING && m.reminders[0].next_due==DAY+2*86400+8*3600);
}
static void keys(void) {
    care_model_t m;care_model_init(&m);strcpy(m.family,"Mia");
    patch(&m,"{\"add_message\":{\"from\":\"Mia\",\"text\":\"Home at seven\"}}",DAY);
    assert(care_model_key(&m,CARE_GREEN,DAY+10,0,10000000,&hooks));assert(m.messages[0].read && !strcmp(event_type,"read"));
    assert(!care_model_key(&m,CARE_GREEN,DAY+11,0,11000000,&hooks));
    assert(care_model_key(&m,CARE_LEFT,DAY+12,0,12000000,&hooks));assert(!strcmp(event_type,"checkin") && strstr(m.footer,"OK today"));
    assert(m.last_checkin==DAY+12);
    assert(care_model_key(&m,CARE_RIGHT,DAY+15,0,15000000,&hooks));assert(!strcmp(event_type,"call_request") && !strcmp(m.footer,"Asked Mia to call you"));
    assert(care_model_key(&m,CARE_GREEN,DAY+18,0,18000000,&hooks));assert(!strcmp(m.footer,"Nothing to confirm") && soft==4);
}
static void persistence(void) {
    care_model_t m,out;care_model_init(&m);patch(&m,reminders,DAY+7*3600);bool changed;
    care_model_step(&m,DAY+8*3600,0,&hooks,&changed);assert(care_store_save(&m));
    memset(&out,0,sizeof(out));assert(care_store_load(&out));assert(out.reminders[0].status==CARE_DUE && out.reminders[0].chimes==1);
    assert(!strcmp(out.family,"Mia"));stored[stored_size-1]^=1;assert(!care_store_load(&out));
    nvs_fail=true;assert(!care_store_save(&m));
}
static void rtc(void) {
    uint8_t encoded[8],raw[9]={0};int64_t decoded;
    const int64_t epochs[]={1704067200LL,1709164799LL,1730707200LL,4102444799LL};
    for(unsigned i=0;i<4;i++){assert(reterminal_io_rtc_encode(epochs[i],encoded));memcpy(raw+2,encoded+1,7);assert(reterminal_io_rtc_decode(raw,&decoded)&&decoded==epochs[i]);}
    raw[2]|=0x80;assert(!reterminal_io_rtc_decode(raw,&decoded));raw[2]&=0x7F;
    raw[0]=0x20;assert(!reterminal_io_rtc_decode(raw,&decoded));raw[0]=0;
    raw[3]=0x6A;assert(!reterminal_io_rtc_decode(raw,&decoded));
    assert(reterminal_io_rtc_encode(1709164799LL,encoded));memcpy(raw+2,encoded+1,7);
    raw[5]=0x30;assert(!reterminal_io_rtc_decode(raw,&decoded));
    assert(!reterminal_io_rtc_encode(0,encoded));
}
static bool ready(void) {return allowed;}
static void pump(void) {if(!setjmp(idle))care_task(NULL);}
static void command(const char *name,const char *json) {
    cJSON *params=json?cJSON_Parse(json):NULL;unsigned before=writes;
    cJSON *async=care_board_command(name,params,"care-1",1);cJSON_Delete(params);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(async,"_async")) && writes==before);cJSON_Delete(async);pump();
    assert(last_result);
}
static void controller(void) {
    clock_epoch=DAY+7*3600;assert(care_board_init(ready));pump();assert(rtc_writes==1);
    command("care.update",reminders);assert(cJSON_IsTrue(cJSON_GetObjectItem(last_result,"ok")) && s_model->active);
    assert(refreshes==1);clock_epoch=DAY+8*3600;pump();assert(led_due);
    unsigned before=refreshes;clock_epoch+=300;pump();assert(refreshes==before);
    allowed=false;assert(!care_board_key(CARE_GREEN));allowed=true;
    assert(care_board_key(CARE_GREEN));pump();assert(!led_due && s_model->reminders[0].status==CARE_DONE);
    assert(soft==1 && silenced==0);
    command("care.state",NULL);cJSON *payload=cJSON_GetObjectItem(last_result,"payload");
    assert(cJSON_IsTrue(cJSON_GetObjectItem(payload,"clock_valid")) && cJSON_IsObject(cJSON_GetObjectItem(payload,"sensor")));
    command("care.clear",NULL);assert(!s_model->active);before=refreshes;
    clock_epoch=DAY+86400;pump();assert(refreshes==before);
    command("care.update","{\"add_message\":{\"from\":\"Mia\",\"text\":\"See you soon\"}}");assert(s_model->active && soft==2);
    care_model_t snapshot=*s_model;
    command("care.update","{\"person\":\"Changed\",\"reminders\":[{}]}");
    assert(cJSON_IsFalse(cJSON_GetObjectItem(last_result,"ok")) && !strcmp(s_model->person,snapshot.person));
    nvs_fail=true;command("care.update","{\"person\":\"Changed\"}");assert(!strcmp(s_model->person,snapshot.person));nvs_fail=false;
    command("care.update","{\"now_epoch\":1730707200,\"utc_offset_min\":480}");assert(clock_offset==480 && rtc_writes>=2);
    assert(wait_ticks>0);
    assert(deadline_ticks(86400000)==86400LL*configTICK_RATE_HZ);
    assert(deadline_ticks(-1)==portMAX_DELAY && deadline_ticks(0)==1);
    for(int i=0;i<8;i++) {
        cJSON *r=care_board_command("care.state",NULL,"care-1",1);
        assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"_async")));cJSON_Delete(r);
    }
    cJSON *busy=care_board_command("care.state",NULL,"care-1",1);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(busy,"ok")));cJSON_Delete(busy);pump();
    nvs_fail=true;assert(care_board_key(CARE_LEFT));pump();assert(!s_storage_ok);
    nvs_fail=false;uptime+=61000000;pump();assert(s_storage_ok);
    care_model_t saved;assert(care_store_load(&saved)&&saved.last_checkin==clock_epoch);
}
static void preview(const char *name,const care_model_t *m,int64_t now) {
    const char *dir=getenv("CARD_PREVIEW_DIR");if(!dir)return;mkdir(dir,0755);
    uint8_t pixels[800*480/8];epaper_cards_canvas_t canvas;
    assert(epaper_cards_canvas(&canvas,pixels,sizeof(pixels),800,480,1));
    care_model_render(&canvas,m,now,0,true,22.5,48);
    char path[4096];snprintf(path,sizeof(path),"%s/%s.pgm",dir,name);FILE *file=fopen(path,"wb");assert(file);
    fprintf(file,"P5\n800 480\n255\n");for(int y=0;y<480;y++)for(int x=0;x<800;x++)fputc(epaper_cards_get(&canvas,x,y)==CARD_WHITE?255:0,file);
    fclose(file);
}
static void previews(void) {
    care_model_t m;care_model_init(&m);
    patch(&m,"{\"family\":\"Mia\",\"weather\":{\"text\":\"Rain later - take an umbrella\",\"icon\":\"rainy\",\"temp\":19},\"add_message\":{\"from\":\"Mia\",\"text\":\"Dad, I'll be home for dinner at seven. Love you!\"},\"reminders\":[{\"id\":\"pill\",\"time\":\"08:00\",\"label\":\"Blood pressure pill\",\"icon\":\"medication\",\"repeat\":\"daily\"},{\"id\":\"walk\",\"time\":\"10:00\",\"label\":\"Morning walk\",\"icon\":\"sunny\",\"repeat\":\"daily\"},{\"id\":\"lunch\",\"time\":\"12:00\",\"label\":\"Lunch\",\"icon\":\"restaurant\",\"repeat\":\"daily\"},{\"id\":\"carer\",\"time\":\"14:00\",\"label\":\"Carer visits\",\"icon\":\"elderly\",\"repeat\":\"once\"}]}",DAY+7*3600);
    preview("care_day",&m,DAY+7*3600);bool changed;
    care_model_step(&m,DAY+8*3600,0,&hooks,&changed);preview("care_due",&m,DAY+8*3600);
    care_model_step(&m,DAY+8*3600+1800,0,&hooks,&changed);preview("care_missed",&m,DAY+8*3600+1800);
    assert(care_model_key(&m,CARE_GREEN,DAY+8*3600+1980,0,10000000,&hooks));
    preview("care_done",&m,DAY+8*3600+1980);
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"validation"))validation();else if(!strcmp(argv[1],"engine"))engine();
    else if(!strcmp(argv[1],"keys"))keys();else if(!strcmp(argv[1],"store"))persistence();
    else if(!strcmp(argv[1],"rtc"))rtc();else if(!strcmp(argv[1],"controller"))controller();else if(!strcmp(argv[1],"preview"))previews();else assert(false);
    cJSON_Delete(last_result);puts("ok");return 0;
}
