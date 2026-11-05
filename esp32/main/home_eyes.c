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

#include "home_eyes.h"
#include "home_eyes_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config_store.h"
#include "gadget_clock.h"
#include "gadget_events.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "muse_home_eyes_ui.h"
#include "muse_state.h"
#include "muse_voice.h"
#include "boards/watcher_camera.h"

static const char *TAG = "link.home.eyes";
static EXT_RAM_BSS_ATTR home_eyes_state_t s_state;
static EXT_RAM_BSS_ATTR home_eyes_state_t s_snapshot;
static EXT_RAM_BSS_ATTR uint8_t s_thumbnail[HOME_EYES_PIXELS];
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;
static uint32_t s_revision, s_saved;
static bool s_chime;
static EXT_RAM_BSS_ATTR char s_error[80];

#define HOME_EYES_STACK_BYTES 4096

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static int64_t epoch(void) { return gadget_clock_valid() ? gadget_clock_epoch() : 0; }
static void notify(void) { if (s_task) xTaskNotifyGive(s_task); }

static cJSON *busy(void)
{
    return cJSON_Parse("{\"ok\":false,\"error\":{\"code\":\"busy\",\"message\":\"Home Eyes is busy; retry shortly\"}}");
}

cJSON *home_eyes_command(const char *command, const cJSON *params)
{
    if (!s_task || xSemaphoreTake(s_mutex, 0) != pdTRUE) return busy();
    bool changed, chime;
    if (home_eyes_expire(&s_state, epoch())) s_revision++;
    cJSON *r = home_eyes_run(&s_state, command, params, now_ms(), epoch(), &changed, &chime);
    if (changed) s_revision++;
    if (chime) s_chime = true;
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(r, "payload");
    if (payload) {
        cJSON_AddStringToObject(payload, "last_error", s_error);
        cJSON_AddBoolToObject(payload, "saved", s_saved == s_revision);
    }
    xSemaphoreGive(s_mutex);
    notify();
    return r;
}

static void publish(const home_eyes_state_t *s)
{
    static uint32_t last_card;
    muse_home_eyes_view_t v = { .active = s->active, .waiting = s->waiting,
                              .next_ms = s->next_ms, .alert = s->alert };
    strcpy(v.name, s->name);
    if (s->alert || (s->verdict && !s->message)) {
        v.card_id = s->verdict_id;
        snprintf(v.heading, sizeof(v.heading), "%s", s->alert ? "HOME EYES ALERT" : "HOME EYES VERDICT");
        strcpy(v.text, s->verdict_text);
    } else if (s->message) {
        v.card_id = s->message_id;
        snprintf(v.heading, sizeof(v.heading), "From %s", s->from);
        strcpy(v.text, s->text);
        if (s->message_epoch) {
            struct tm local;
            int64_t current = epoch();
            if (current && gadget_clock_local(&local)) {
                time_t offset = mktime(&local) - (time_t)current;
                time_t received = (time_t)s->message_epoch + offset;
                struct tm received_tm;
                gmtime_r(&received, &received_tm);
                strftime(v.time, sizeof(v.time), "%m-%d %H:%M", &received_tm);
            }
        }
        if (!v.time[0]) strcpy(v.time, "Time not synchronized");
    }
    muse_home_eyes_publish(&v);
    if (v.card_id && v.card_id != last_card) {
        muse_state_set_asleep(false);
        muse_state_poke();
    }
    last_card = v.card_id;
}

static bool post_event(const home_eyes_state_t *s, unsigned event)
{
    char summary[512];
    if (event == HOME_EYES_CHANGE)
        snprintf(summary, sizeof(summary), "Home Eyes saw a change at \"%s\". The user asked: \"%s\". Call camera.capture to look, then answer with watch.verdict.", s->name, s->question);
    else if (event == HOME_EYES_ALERT_ACK)
        strcpy(summary, "Someone at home acknowledged the alert");
    else snprintf(summary, sizeof(summary), "Someone at home saw your message \"%s...\".", s->seen);
    return gadget_events_post("home-eyes", summary, NULL);
}

// Serialize persistence, captures and event retries on an internal-stack worker.
// 在使用内部内存栈的后台任务中依次处理保存、拍照和事件重试。
static void worker(void *arg)
{
    (void)arg;
    while (!muse_home_eyes_is_ready()) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    int64_t retry_ms = 0;
    bool captured = false;
    static bool stack_logged;
    for (;;) {
        int64_t now = now_ms(), utc = epoch();
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        uint32_t ack = muse_home_eyes_take_ack();
        if (home_eyes_ack(&s_state, ack)) s_revision++;
        if (home_eyes_expire(&s_state, utc)) s_revision++;
        if (s_state.message && utc && s_state.message_epoch && utc >= s_state.message_epoch)
            s_state.message_ms = now - (utc - s_state.message_epoch) * 1000;
        if (home_eyes_reminder(&s_state, now)) { s_chime = true; s_revision++; }
        if (s_state.alert && now >= s_state.alarm_ms) {
            s_chime = true; s_state.alarm_ms = now + 5000;
        }
        bool chime = s_chime; s_chime = false;
        s_snapshot = s_state;
        const home_eyes_state_t *snapshot = &s_snapshot;
        uint32_t revision = s_revision;
        bool save = revision != s_saved;
        bool capture = home_eyes_due(snapshot, now) && (!snapshot->until || utc);
        xSemaphoreGive(s_mutex);
        publish(snapshot);
        if (chime) {
            muse_state_set_asleep(false);
            muse_state_poke();
            muse_voice_request_chirp();
        }
        if (save) {
            char *json = home_eyes_save(snapshot, now, utc);
            bool ok = json && config_set_str("home_eyes", json);
            free(json);
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            if (ok) s_saved = revision;
            else snprintf(s_error, sizeof(s_error), "state could not be saved");
            xSemaphoreGive(s_mutex);
            if (ok && captured && !stack_logged) {
                ESP_LOGI("link.stack", "home_eyes stack high water mark: %u bytes free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL) * (unsigned)sizeof(StackType_t));
                stack_logged = true;
            }
        }
        if (snapshot->events && now >= retry_ms) {
            unsigned delivered = 0;
            for (unsigned e = 1; e <= HOME_EYES_MESSAGE_ACK; e <<= 1)
                if ((snapshot->events & e) && post_event(snapshot, e)) delivered |= e;
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            if ((delivered & HOME_EYES_CHANGE) &&
                (strcmp(snapshot->name, s_state.name) || strcmp(snapshot->question, s_state.question)))
                delivered &= ~HOME_EYES_CHANGE;
            if ((delivered & HOME_EYES_MESSAGE_ACK) && strcmp(snapshot->seen, s_state.seen))
                delivered &= ~HOME_EYES_MESSAGE_ACK;
            if (delivered) { s_state.events &= ~delivered; s_revision++; }
            xSemaphoreGive(s_mutex);
            retry_ms = now + 5000;
        }
        if (capture) {
            const char *error = NULL;
            bool ok = watcher_camera_thumbnail(s_thumbnail, HOME_EYES_SIDE, &error);
            if (ok) captured = true;
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            if (revision == s_revision && home_eyes_due(&s_state, now_ms())) {
                if (ok) { home_eyes_frame(&s_state, s_thumbnail, now_ms()); s_error[0] = 0; }
                else {
                    s_state.next_ms = now_ms() + s_state.interval_min * 60000LL;
                    snprintf(s_error, sizeof(s_error), "%s", error ? error : "camera check failed");
                    ESP_LOGW(TAG, "camera check failed");
                }
                s_revision++;
            }
            xSemaphoreGive(s_mutex);
            continue;
        }
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        now = now_ms(); utc = epoch();
        int64_t deadline = now + 3600000;
#define EARLIER(value) do { int64_t t = (value); if (t < deadline) deadline = t; } while (0)
        if (home_eyes_due(&s_state, now) && (!s_state.until || utc)) deadline = now;
        else if (s_state.active && !s_state.waiting && !s_state.alert && (!s_state.until || utc)) EARLIER(s_state.next_ms);
        if (s_state.active && s_state.until) {
            if (utc) EARLIER(now + (s_state.until - utc) * 1000);
            else EARLIER(now + 1000);
        }
        if (s_state.message && !s_state.reminded) EARLIER(s_state.message_ms + 600000);
        if (s_state.alert) EARLIER(s_state.alarm_ms);
        if (s_state.events) EARLIER(retry_ms);
        if (s_revision != s_saved) EARLIER(now + 5000);
        if (s_revision != revision) deadline = now;
#undef EARLIER
        xSemaphoreGive(s_mutex);
        if (deadline > now) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(deadline - now));
    }
}

bool home_eyes_init(void)
{
    if (s_task) return true;
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return false;
    home_eyes_reset(&s_state);
    char *json = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json) { vSemaphoreDelete(s_mutex); s_mutex = NULL; return false; }
    if (config_get_str("home_eyes", json, 8192))
        home_eyes_restore(&s_state, json, now_ms(), epoch());
    free(json);
    s_revision = 1;
    muse_home_eyes_bind(notify);
    if (xTaskCreate(worker, "home_eyes", HOME_EYES_STACK_BYTES, NULL, 3, &s_task) != pdPASS) {
        muse_home_eyes_bind(NULL);
        vSemaphoreDelete(s_mutex); s_mutex = NULL; return false;
    }
    return true;
}
