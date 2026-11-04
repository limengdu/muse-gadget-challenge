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
#include "../main/air_guardian.c"

static air_request_t pending;
static bool queued, reject_queue, reject_task;
static TaskFunction_t worker;
static jmp_buf done;
static int iterations, draws, posted;
static int64_t fake_time = 10000000;
static char stored[512], stored_alerts[3072];
static cJSON *reply;
static noise_ctrl_session_generation_t reply_generation;
static char reply_id[96];

QueueHandle_t xQueueCreate(unsigned n, unsigned size) {assert(n == 4 && size == sizeof(pending));return (void *)1;}
void vQueueDelete(QueueHandle_t q) {(void)q;}
BaseType_t xQueueSend(QueueHandle_t q,const void *p,TickType_t wait) {
    assert(q && wait == 0);
    if (reject_queue) return pdFALSE;
    pending = *(const air_request_t *)p; queued = true; return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q,void *p,TickType_t wait) {
    assert(q && wait == 1000);
    if (iterations++) longjmp(done, 1);
    if (!queued) return pdFALSE;
    *(air_request_t *)p = pending; queued = false; return pdTRUE;
}
BaseType_t xTaskCreate(TaskFunction_t fn,const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle) {
    (void)name; (void)arg; (void)priority; (void)handle;
    assert(stack == 8192);
    worker = fn; return reject_task ? pdFALSE : pdPASS;
}
int64_t esp_timer_get_time(void) {return fake_time;}
bool config_get_str(const char *key,char *out,size_t n) {
    const char *value = !strcmp(key,"air_settings") ? stored : stored_alerts;
    if (!value[0]) return false;
    snprintf(out,n,"%s",value); return true;
}
bool config_set_str(const char *key,const char *value) {
    if (!strcmp(key,"air_settings")) snprintf(stored,sizeof(stored),"%s",value);
    else {assert(!strcmp(key,"air_alerts")); snprintf(stored_alerts,sizeof(stored_alerts),"%s",value);}
    return true;
}
bool gadget_clock_valid(void) {return true;}
int64_t gadget_clock_epoch(void) {return 1760000000;}
int gadget_clock_minutes_of_day(void) {return 23*60;}
bool gadget_events_post(const char *source,const char *summary,const cJSON *data) {
    assert(!strcmp(source,"Air Guardian") && summary && data); posted++; return true;
}
bool led_status_air_draw(const uint16_t *pixels) {assert(pixels);draws++;return true;}
cJSON *sensecap_sensors_command(void) {
    return cJSON_Parse("{\"ok\":true,\"payload\":{\"co2\":{\"value\":1600},\"tvoc\":{\"value\":100},\"temperature\":{\"value\":23},\"humidity\":{\"value\":45}}}");
}
void noise_ctrl_send_command_result(noise_ctrl_session_generation_t generation,const char *id,cJSON *result) {
    cJSON_Delete(reply);reply=result;reply_generation=generation;snprintf(reply_id,sizeof(reply_id),"%s",id);
}
static void tick(void) {iterations=0;if (!setjmp(done)) worker(NULL);}
int main(void) {
    cJSON *r=air_guardian_command("air.status",NULL,"req",1);
    assert(!cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    reject_task=true;assert(!air_guardian_init() && !s_requests);
    reject_task=false;assert(air_guardian_init());
    cJSON *params=cJSON_Parse("{\"room\":\"Nursery\",\"co2_ppm\":1700}");
    r=air_guardian_command("air.set_thresholds",params,"request-17",17);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"_async")));cJSON_Delete(r);cJSON_Delete(params);
    tick();assert(reply_generation==17 && !strcmp(reply_id,"request-17"));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(reply,"ok")) && strstr(stored,"Nursery") && draws==1);
    assert(!strcmp(s_core.settings.room,"Nursery"));
    reject_queue=true;r=air_guardian_command("air.status",NULL,"req",1);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);reject_queue=false;
    air_guardian_cycle_view();tick();assert(s_core.settings.view==AIR_NUMBERS && strstr(stored,"numbers"));
    s_core.settings.view=AIR_NIGHT;strcpy(s_core.settings.room,"Reset");restore();
    assert(s_core.settings.view==AIR_NUMBERS && !strcmp(s_core.settings.room,"Nursery"));
    strcpy(stored,"{\"room\":\"Changed\",\"co2_ppm\":0,\"view\":\"auto\"}");restore();
    assert(s_core.settings.view==AIR_NUMBERS && !strcmp(s_core.settings.room,"Nursery"));
    fake_time+=6000000;tick();assert(s_core.valid==15 && s_core.count>=1 && posted==0);
    s_core.settings.limits[0]=1500;fake_time+=6000000;tick();
    assert(posted==1 && stored_alerts[0]);
    memset(s_core.alarms,0,sizeof(s_core.alarms));restore_alerts();
    assert(s_core.alarms[0].announced && strstr(s_core.alarms[0].last_message,"Nursery"));
    strcpy(stored_alerts,"[]");restore_alerts();assert(s_core.alarms[0].announced);
    cJSON_Delete(reply);free(s_core.history);free(s_pixels);puts("ok");return 0;
}
