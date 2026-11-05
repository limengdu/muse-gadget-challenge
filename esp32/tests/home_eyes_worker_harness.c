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
#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main/home_eyes.c"

static jmp_buf idle;
static int64_t current_ms;
static unsigned captures, posts, tones;
static unsigned stack_logs;
static bool ready = true, accept = true, store_ok = true, camera_ok = true;
static uint32_t acknowledgement;
static uint8_t image[HOME_EYES_PIXELS];
static char stored[8192], last_summary[512];
static muse_home_eyes_view_t view;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t m, int ticks) { (void)m; (void)ticks; return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t m) { (void)m; return pdTRUE; }
void vSemaphoreDelete(SemaphoreHandle_t m) { (void)m; }
BaseType_t xTaskCreate(TaskFunction_t f, const char *n, unsigned bytes, void *a, unsigned p, TaskHandle_t *h)
{ (void)f; (void)a; (void)p; assert(!strcmp(n, "home_eyes") && bytes == 4096); *h = (void *)1; return pdPASS; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t h) { (void)h; return 2048; }
void record_stack_log(const char *tag, const char *fmt, ...)
{
    assert(!strcmp(tag, "link.stack") && captures && stored[0]);
    va_list args; va_start(args, fmt); assert(va_arg(args, unsigned) == 2048); va_end(args);
    stack_logs++;
}
void xTaskNotifyGive(TaskHandle_t h) { (void)h; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks)
{ (void)clear; assert(ticks > 0); longjmp(idle, 1); }
int64_t esp_timer_get_time(void) { return current_ms * 1000; }
bool gadget_clock_valid(void) { return true; }
int64_t gadget_clock_epoch(void) { return 1800000000 + current_ms / 1000; }
bool gadget_clock_local(struct tm *t) { time_t now = gadget_clock_epoch(); return gmtime_r(&now, t) != NULL; }
bool config_get_str(const char *key, char *out, size_t n)
{ (void)key; if (!stored[0]) return false; snprintf(out, n, "%s", stored); return true; }
bool config_set_str(const char *key, const char *value)
{ assert(!strcmp(key, "home_eyes")); if (!store_ok) return false; snprintf(stored, sizeof(stored), "%s", value); return true; }
void muse_home_eyes_bind(void (*fn)(void)) { (void)fn; }
bool muse_home_eyes_is_ready(void) { return ready; }
uint32_t muse_home_eyes_take_ack(void) { uint32_t id = acknowledgement; acknowledgement = 0; return id; }
void muse_home_eyes_publish(const muse_home_eyes_view_t *v) { view = *v; }
void muse_state_set_asleep(bool asleep) { assert(!asleep); }
void muse_state_poke(void) {}
void muse_voice_request_chirp(void) { tones++; }
bool watcher_camera_thumbnail(uint8_t *pixels, size_t side, const char **err)
{ assert(side == HOME_EYES_SIDE); captures++; if (!camera_ok) { *err = "camera busy"; return false; } memcpy(pixels, image, sizeof(image)); return true; }
bool gadget_events_post(const char *source, const char *summary, const cJSON *data)
{ assert(!strcmp(source, "home-eyes")); assert(!data); posts++; snprintf(last_summary, sizeof(last_summary), "%s", summary); return accept; }

static void pump(void) { if (!setjmp(idle)) worker(NULL); }
static void cmd(const char *command, const char *json)
{
    cJSON *p = json ? cJSON_Parse(json) : NULL;
    cJSON *r = home_eyes_command(command, p);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(r, "ok")));
    cJSON_Delete(r); cJSON_Delete(p);
}

int main(void)
{
    assert(home_eyes_init()); assert(home_eyes_init());
    memset(image, 80, sizeof(image));
    cmd("watch.start", "{\"name\":\"printer\",\"question\":\"Has the print failed?\",\"interval_min\":2}");
    pump(); assert(captures == 1 && posts == 0 && stored[0] && stack_logs == 1);
    current_ms = 119999; pump(); assert(captures == 1);
    for (int i = 0; i < HOME_EYES_PIXELS; i++) image[i] = i % 32 < 16 ? 180 : 80;
    current_ms = 120000; accept = false; pump();
    assert(captures == 2 && s_state.waiting && (s_state.events & HOME_EYES_CHANGE));
    assert(strstr(last_summary, "Call camera.capture"));
    accept = true; current_ms += 5000; pump();
    assert(!(s_state.events & HOME_EYES_CHANGE));
    unsigned posted = posts; current_ms += 600000; pump(); assert(posts == posted && captures == 2);
    cmd("watch.verdict", "{\"text\":\"Print detached.\",\"alert\":true}");
    pump(); assert(view.alert && tones);
    unsigned sounded = tones; current_ms += 5000; pump(); assert(tones == sounded + 1);
    acknowledgement = view.card_id; pump(); assert(!s_state.alert);
    assert(strstr(last_summary, "acknowledged the alert"));
    cmd("home.say", "{\"text\":\"Home in 20 minutes.\",\"from\":\"Parent\"}");
    pump(); assert(view.card_id && !strcmp(view.heading, "From Parent"));
    sounded = tones; current_ms += 599999; pump(); assert(tones == sounded);
    current_ms++; pump(); assert(tones == sounded + 1);
    current_ms += 600000; pump(); assert(tones == sounded + 1);
    acknowledgement = view.card_id; pump(); assert(!s_state.message && strstr(last_summary, "saw your message"));
    camera_ok = false; current_ms += 120000; pump(); assert(s_error[0]);
    assert(s_state.next_ms > current_ms);
    store_ok = false; cmd("watch.stop", NULL); pump(); assert(s_saved != s_revision);
    store_ok = true; current_ms += 5000; pump(); assert(s_saved == s_revision);
    assert(stack_logs == 1);
    puts("Home Eyes worker: OK");
    return 0;
}
