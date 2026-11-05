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
#include <stdarg.h>
#include <stdio.h>
#include "../main/air_guardian.c"

#define NOW 1793793600LL
static air_request_t pending;
static bool queued, reject_queue, reject_task, reject_store;
static TaskFunction_t worker;
static bool worker_psram, saving, display_active = true, clock_known = true;
static unsigned worker_creations, save_creations, notifications, worker_logs, save_logs;
static unsigned draws, redraw_logs, step;
static char redraw_reason[24];
static gadget_clock_change_cb clock_cb;
#if CONFIG_SPIRAM
static bool reject_save_task;
static void *save_arg;
#endif
static jmp_buf done;
static int64_t fake_epoch = NOW;
static int offset;
static char stored[1024];
static cJSON *reply;
static noise_ctrl_session_generation_t reply_generation;
static char reply_id[96];
static void scenario(TickType_t wait);

QueueHandle_t xQueueCreate(unsigned n, unsigned size) {assert(n == 4 && size == sizeof(pending));return (void *)1;}
void vQueueDelete(QueueHandle_t q) {(void)q;}
BaseType_t xQueueSend(QueueHandle_t q,const void *p,TickType_t wait) {
    assert(q && wait == 0);
    if (reject_queue) return pdFALSE;
    assert(!queued);
    pending = *(const air_request_t *)p; queued = true; return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q,void *p,TickType_t wait) {
    assert(q && !queued);
    assert(redraw_logs == draws);
    scenario(wait);
    if (!queued) return pdFALSE;
    *(air_request_t *)p = pending; queued = false; return pdTRUE;
}
BaseType_t xTaskCreate(TaskFunction_t fn,const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle) {
    assert(!handle);
#if CONFIG_SPIRAM
    assert(fn == save_task && !strcmp(name,"air_save") && stack == 4096 && priority == 3);
    save_creations++;
    if (reject_save_task) return pdFALSE;
    save_arg = arg;
    return pdPASS;
#else
    assert(fn == air_task && !strcmp(name,"outdoor_air") && stack == 8192 && priority == 2 && !arg);
    worker_creations++;
    worker = fn; return reject_task ? pdFALSE : pdPASS;
#endif
}
BaseType_t xTaskCreateWithCaps(TaskFunction_t fn,const char *name,unsigned stack,void *arg,unsigned priority,
                              TaskHandle_t *handle,unsigned caps) {
    assert(CONFIG_SPIRAM && fn == air_task && !strcmp(name,"outdoor_air"));
    assert(stack == 8192 && !arg && priority == 2 && !handle && caps == MALLOC_CAP_SPIRAM);
    worker_creations++; worker_psram = true; worker = fn;
    return reject_task ? pdFALSE : pdPASS;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void) {return (void *)1;}
void xTaskNotifyGive(TaskHandle_t task) {assert(task == (void *)1 && saving); notifications++;}
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t wait) {
    assert(clear == pdTRUE && wait == portMAX_DELAY);
#if CONFIG_SPIRAM
    assert(save_arg && !notifications);
    saving = true; save_task(save_arg); saving = false; save_arg = NULL;
#endif
    assert(notifications == 1);
    notifications = 0; return 1;
}
void vTaskDelete(TaskHandle_t task) {assert(!task && saving);}
void stack_monitor_record(stack_monitor_t *state) {
    assert(!state);
    if (saving) save_logs++;
    else worker_logs++;
}
void air_test_log(const char *tag,const char *format,...) {
    assert(!strcmp(tag,"link.air") && !strcmp(format,"redraw reason=%s"));
    va_list args;va_start(args,format);
    snprintf(redraw_reason,sizeof(redraw_reason),"%s",va_arg(args,const char *));
    va_end(args);redraw_logs++;
}
bool config_get_str(const char *key,char *out,size_t n) {
    assert(!strcmp(key,"outdoor_air"));
    if (!stored[0]) return false;
    snprintf(out,n,"%s",stored); return true;
}
bool config_set_str(const char *key,const char *value) {
    assert(!strcmp(key,"outdoor_air"));
    assert(!worker_psram || saving);
    if (reject_store) return false;
    snprintf(stored,sizeof(stored),"%s",value); return true;
}
void gadget_clock_set_change_cb(gadget_clock_change_cb cb) {clock_cb=cb;}
bool gadget_clock_valid(void) {return clock_known;}
int64_t gadget_clock_epoch(void) {return fake_epoch;}
int gadget_clock_offset_minutes(void) {return offset;}
bool led_status_air_active(void) {return display_active;}
bool led_status_air_draw(const uint16_t *pixels) {assert(pixels && display_active);draws++;return true;}
void noise_ctrl_send_command_result(noise_ctrl_session_generation_t generation,const char *id,cJSON *result) {
    cJSON_Delete(reply);reply=result;reply_generation=generation;snprintf(reply_id,sizeof(reply_id),"%s",id);
}
static void issue(const char *command,const char *json) {
    cJSON *params=json?cJSON_Parse(json):NULL;
    cJSON *r=air_guardian_command(command,params,"request-17",17);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"_async")));
    cJSON_Delete(r);cJSON_Delete(params);
}
static void update(int aqi,int64_t observed,double pm25) {
    char json[512];
    snprintf(json,sizeof(json),"{\"aqi\":%d,\"pm25\":%.2f,\"category\":\"good\",\"advice\":\"Source advice\",\"place\":\"Home city\",\"updated_utc\":%lld}",
             aqi,pm25,(long long)observed);
    issue("air.update",json);
}
static void expect(unsigned count,const char *reason) {
    assert(draws==count && redraw_logs==count);
    if (reason) assert(!strcmp(redraw_reason,reason));
}

// Runs one continuous worker while queue waits advance a simulated wall clock.
// 在同一个连续后台任务中，以队列等待推进模拟时钟。
static void scenario(TickType_t wait) {
    switch(step++) {
    case 0:
        expect(1,"initial");assert(wait==portMAX_DELAY && clock_cb);update(42,NOW,7.1);break;
    case 1:
        expect(2,"snapshot");assert(wait==AIR_STALE_S*1000 && s_core.snapshot.aqi==42);
        assert(reply_generation==17 && !strcmp(reply_id,"request-17"));
        assert(cJSON_IsTrue(cJSON_GetObjectItem(reply,"ok")) && strstr(stored,"Home city"));
        fake_epoch+=3600;issue("air.status",NULL);break;
    case 2:
        expect(2,NULL);assert(wait==(AIR_STALE_S-3600)*1000);update(42,NOW,7.1);break;
    case 3:
        expect(2,NULL);issue("air.set_view","{\"view\":\"ambient\"}");break;
    case 4:
        expect(2,NULL);air_guardian_cycle_view();break;
    case 5:
        expect(3,"view");assert(s_core.view==AIR_NUMBERS && strstr(stored,"numbers"));
        reject_store=true;air_guardian_cycle_view();break;
    case 6:
        expect(3,NULL);assert(s_core.view==AIR_NUMBERS);reject_store=false;
#if CONFIG_SPIRAM
        reject_save_task=true;
#else
        reject_store=true;
#endif
        air_guardian_cycle_view();break;
    case 7:
        expect(3,NULL);assert(s_core.view==AIR_NUMBERS);reject_store=false;
#if CONFIG_SPIRAM
        reject_save_task=false;
#endif
        issue("air.update","{\"aqi\":-5}");break;
    case 8: {
        expect(3,NULL);assert(cJSON_IsFalse(cJSON_GetObjectItem(reply,"ok")));
        reject_queue=true;cJSON *r=air_guardian_command("air.status",NULL,"req",1);
        assert(cJSON_IsFalse(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);reject_queue=false;
        offset=1439-(int)(fake_epoch/60%1440);
        if(offset>840)offset-=1440;
        clock_cb();break;
    }
    case 9:
        expect(4,"clock");assert((fake_epoch/60+offset)%1440==1439);
        fake_epoch+=60;clock_cb();break;
    case 10:
        expect(4,NULL);assert(wait==(AIR_STALE_S-3660)*1000 && (fake_epoch/60+offset)%1440==0);
        fake_epoch=NOW+AIR_STALE_S-1;clock_cb();break;
    case 11:
        expect(4,NULL);assert(wait==1000);fake_epoch++;break;
    case 12:
        expect(5,"age");assert(wait==portMAX_DELAY);
        fake_epoch+=3600;issue("air.status",NULL);break;
    case 13:
        expect(5,NULL);display_active=false;air_guardian_display_changed();break;
    case 14:
        expect(5,NULL);assert(wait==portMAX_DELAY);update(43,fake_epoch,7.1);break;
    case 15:
        expect(5,NULL);display_active=true;air_guardian_display_changed();break;
    case 16:
        expect(6,"display");air_guardian_cycle_view();break;
    case 17:
        expect(7,"view");assert(s_core.view==AIR_NIGHT);air_guardian_cycle_view();break;
    case 18:
        expect(8,"view");assert(s_core.view==AIR_AMBIENT);update(44,fake_epoch+60,7.1);break;
    case 19:
        expect(9,"snapshot");assert(wait==60000);fake_epoch+=60;break;
    case 20:
        expect(10,"age");clock_known=false;clock_cb();break;
    case 21:
        expect(11,"age");assert(wait==portMAX_DELAY);clock_known=true;clock_cb();break;
    case 22:
        expect(12,"age");update(45,fake_epoch,7.1);break;
    case 23:
        expect(13,"snapshot");update(45,fake_epoch,8.2);break;
    case 24:
        expect(13,NULL);issue("air.set_view","{\"view\":\"numbers\"}");break;
    case 25:
        expect(14,"view");update(45,fake_epoch,8.23);break;
    case 26:
        expect(14,NULL);update(45,fake_epoch,8.3);break;
    case 27:
        expect(15,"snapshot");issue("air.status",NULL);
        reject_queue=true;air_guardian_display_changed();reject_queue=false;break;
    case 28:
        expect(16,"display");longjmp(done,1);
    default: assert(false);
    }
}
int main(void) {
    cJSON *r=air_guardian_command("air.status",NULL,"req",1);
    assert(!cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    reject_task=true;assert(!air_guardian_init() && !s_requests && !clock_cb);
    reject_task=false;assert(air_guardian_init());
    assert(worker_creations==2 && worker_psram==!!CONFIG_SPIRAM);
    if (!setjmp(done)) worker(NULL);
    assert(worker_logs==1);
#if CONFIG_SPIRAM
    assert(save_creations>1 && save_logs==save_creations-1);
#else
    assert(save_creations==0 && save_logs==0);
#endif
    air_core_init(&s_core);restore();
    assert(s_core.view==AIR_NUMBERS && s_core.snapshot.aqi==45 && !strcmp(s_core.snapshot.place,"Home city"));
    strcpy(stored,"{\"version\":1,\"view\":\"night\",\"snapshot\":{}} ");restore();
    assert(s_core.view==AIR_NUMBERS && s_core.snapshot.aqi==45);
    strcpy(stored,"{\"version\":1,\"view\":\"night\",\"snapshot\":null}junk");restore();
    assert(s_core.view==AIR_NUMBERS && s_core.snapshot.aqi==45);
    r=air_guardian_command(NULL,NULL,"req",1);assert(cJSON_IsFalse(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    cJSON_Delete(reply);free(s_pixels);puts("ok");return 0;
}
