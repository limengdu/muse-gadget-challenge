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

#include "kid_board.h"
#include "kid_memory.h"
#include "kid_store.h"
#include "gadget_clock.h"
#include "gadget_events.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "link.kid";
typedef struct {
    bool key;
    char command[24], id[96];
    cJSON *params;
    noise_ctrl_session_generation_t generation;
} kid_job_t;

static kid_model_t *s_model;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_jobs;
static TaskHandle_t s_task;
static bool (*s_keys_ready)(void);
static atomic_bool s_clock_dirty, s_render_pending, s_rendered;
static atomic_llong s_render_at;
static kid_refresh_t s_refresh;
static bool s_storage_ok = true, s_rtc_ok, s_key_seen;
static int64_t s_last_key_us, s_retry_at, s_visible_minute = -1;

static bool keys_ready(void) { return s_task && s_keys_ready && s_keys_ready(); }
void kid_board_notify(void) { if (s_task) xTaskNotifyGive(s_task); }
static void clock_changed(void) { atomic_store(&s_clock_dirty, true); kid_board_notify(); }
static void key_received(reterminal_key_t key) { kid_board_key(key); }

static cJSON *error_result(const char *code, const char *message) {
    cJSON *root = cJSON_CreateObject(), *error = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false); cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message); cJSON_AddItemToObject(root, "error", error); return root;
}

static kid_model_t *clone_model(void) {
    kid_model_t *m = kid_memory_alloc(sizeof(*m));
    if (m) { xSemaphoreTake(s_lock, portMAX_DELAY); *m = *s_model; xSemaphoreGive(s_lock); }
    return m;
}

static void publish(kid_model_t *m) {
    xSemaphoreTake(s_lock, portMAX_DELAY); kid_model_t *old = s_model; s_model = m;
    xSemaphoreGive(s_lock); free(old); kid_refresh_request(&s_refresh);
}

static cJSON *state_result(void) {
    cJSON *root = cJSON_CreateObject(), *payload = kid_model_json(s_model, gadget_clock_epoch(), gadget_clock_offset_minutes());
    cJSON_AddBoolToObject(root, "ok", true); cJSON_AddItemToObject(root, "payload", payload);
    cJSON_AddBoolToObject(payload, "storage_ok", s_storage_ok); cJSON_AddBoolToObject(payload, "rtc_ok", s_rtc_ok);
    return root;
}

static bool post_event(const char *summary, const cJSON *data) {
    uint64_t seq = 0;
    bool ok = gadget_events_post_seq("Kid Board", summary, data, &seq);
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "id"));
    ESP_LOGI(TAG, "event type=kid.done seq=%" PRIu64 " id=%s queued=%d at=%" PRId64,
        seq, id ? id : "-", ok, gadget_clock_epoch()); return ok;
}
static const kid_hooks_t s_hooks = {.event = post_event, .chime = reterminal_io_chime};

// Records the actual render start so queued panel work shares the refresh window.
// 记录实际绘图开始时刻，使面板队列中的工作共用刷新时间窗口。
static void render(epaper_cards_canvas_t *canvas, void *ctx) {
    (void)ctx;
    kid_model_t *m = clone_model();
    if (m) { kid_model_render(canvas, m, gadget_clock_epoch(), gadget_clock_offset_minutes()); free(m); }
    atomic_store(&s_render_at, esp_timer_get_time()); atomic_store(&s_rendered, true);
    atomic_store(&s_render_pending, false); kid_board_notify();
}

bool kid_board_key(reterminal_key_t key) {
    if (!keys_ready() || key != RETERMINAL_GREEN) return false;
    kid_job_t job = {.key = true};
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) return false;
    kid_board_notify(); return true;
}

// Copies invocation data into the worker queue and returns an asynchronous receipt.
// 将调用数据复制到后台队列，并返回异步接收凭据。
cJSON *kid_board_command(const char *command, const cJSON *params,
                          const char *request_id, noise_ctrl_session_generation_t generation) {
    if (!s_task) return error_result("unavailable", "Kid Board is not ready");
    if (!command || (strcmp(command, "kid.set_schedule") && strcmp(command, "kid.set_stars")
        && strcmp(command, "kid.mark") && strcmp(command, "kid.state"))) return error_result("unsupported", "Unknown kid command");
    if (!request_id || !*request_id || strlen(request_id) >= sizeof(((kid_job_t *)0)->id)
        || !cJSON_IsObject(params)) return error_result("invalid_params", "Use an object and a valid request id");
    kid_job_t job = {.generation = generation};
    strcpy(job.command, command); strcpy(job.id, request_id); job.params = cJSON_Duplicate(params, true);
    if (!job.params) return error_result("out_of_memory", "Could not copy kid parameters");
    cJSON *result = cJSON_CreateObject();
    if (!result) { cJSON_Delete(job.params); return NULL; }
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        cJSON_Delete(job.params); cJSON_Delete(result); return error_result("busy", "Kid Board queue is full");
    }
    cJSON_AddBoolToObject(result, "_async", true); kid_board_notify(); return result;
}

static void rollover(void) {
    kid_model_t *m = clone_model();
    if (!m) return;
    if (kid_model_rollover(m, gadget_clock_epoch(), gadget_clock_offset_minutes())) {
        s_storage_ok = kid_store_save(m); publish(m);
    } else free(m);
}

static void process_job(kid_job_t *job) {
    cJSON *result = NULL;
    if (!job->key && !strcmp(job->command, "kid.state")) {
        bool changed; const char *error;
        if (!kid_model_command(s_model, job->command, job->params, gadget_clock_epoch(), gadget_clock_offset_minutes(), &changed, &error))
            result = error_result("invalid_params", error);
        else result = state_result();
    } else {
        kid_model_t *m = clone_model();
        if (!m) result = error_result("out_of_memory", "Could not copy kid state");
        else if (job->key) {
            int64_t us = esp_timer_get_time();
            if (keys_ready() && (!s_key_seen || us - s_last_key_us >= 2000000)
                && kid_model_green(m, gadget_clock_epoch(), gadget_clock_offset_minutes(), &s_hooks)) {
                s_key_seen = true; s_last_key_us = us;
                s_storage_ok = kid_store_save(m); publish(m); m = NULL;
            }
            free(m);
        } else {
            bool changed; const char *error;
            if (!kid_model_command(m, job->command, job->params, gadget_clock_epoch(), gadget_clock_offset_minutes(), &changed, &error)) {
                result = error_result("invalid_params", error); free(m);
            } else if (!changed) { free(m); result = state_result(); }
            else if (!kid_store_save(m)) { free(m); result = error_result("storage_failed", "Could not save kid state"); }
            else { s_storage_ok = true; publish(m); result = state_result(); }
        }
    }
    cJSON_Delete(job->params);
    if (!job->key) noise_ctrl_send_command_result(job->generation, job->id, result);
}

// Waits for commands, minute boundaries, storage retries and the next permitted draw.
// 等待命令、分钟边界、存储重试以及下次允许绘图的时刻。
static void kid_task(void *arg) {
    (void)arg;
    for (;;) {
        if (atomic_exchange(&s_clock_dirty, false) && gadget_clock_valid()) s_rtc_ok = reterminal_io_rtc_write(gadget_clock_epoch());
        rollover();
        kid_job_t job;
        for (unsigned i = 0; i < 8 && xQueueReceive(s_jobs, &job, 0) == pdTRUE; i++) process_job(&job);
        int64_t us = esp_timer_get_time(), now = gadget_clock_epoch();
        if (!s_storage_ok && us >= s_retry_at) { s_storage_ok = kid_store_save(s_model); s_retry_at = us + KID_REFRESH_US; }
        if (s_model->active && s_visible_minute != (gadget_clock_valid() ? now / 60 : -1)) {
            s_visible_minute = gadget_clock_valid() ? now / 60 : -1; kid_refresh_request(&s_refresh);
        }
        if (atomic_exchange(&s_rendered, false)) {
            bool dirty = s_refresh.dirty;
            kid_refresh_started(&s_refresh, atomic_load(&s_render_at)); s_refresh.dirty = dirty;
        }
        int64_t wait_us = gadget_clock_valid() && s_model->active ? (60 - now % 60) * 1000000LL : -1;
        if (keys_ready() && s_model->active && !atomic_load(&s_render_pending)) {
            int64_t draw_wait = kid_refresh_wait(&s_refresh, us);
            if (draw_wait == 0) {
                atomic_store(&s_render_pending, true);
                if (epaper_cards_show(render, NULL)) s_refresh.dirty = false;
                else { atomic_store(&s_render_pending, false); draw_wait = 1000000; }
            }
            if (draw_wait > 0 && (wait_us < 0 || wait_us > draw_wait)) wait_us = draw_wait;
        }
        if (!s_storage_ok) {
            int64_t retry = s_retry_at > us ? s_retry_at - us : 1000;
            if (wait_us < 0 || wait_us > retry) wait_us = retry;
        }
        if (uxQueueMessagesWaiting(s_jobs)) wait_us = 1000;
        TickType_t ticks = wait_us < 0 ? portMAX_DELAY : (TickType_t)((wait_us * configTICK_RATE_HZ + 999999) / 1000000);
        ulTaskNotifyTake(pdTRUE, ticks ? ticks : 1);
    }
}

bool kid_board_init(bool (*ready)(void)) {
    if (s_task || !ready) return false;
    s_keys_ready = ready; s_lock = xSemaphoreCreateMutex(); s_jobs = xQueueCreate(8, sizeof(kid_job_t));
    s_model = kid_memory_alloc(sizeof(*s_model));
    if (!s_lock || !s_jobs || !s_model) return false;
    kid_model_init(s_model); kid_store_load(s_model);
    if (s_model->active) kid_refresh_request(&s_refresh);
    if (!reterminal_io_init(keys_ready, key_received)) ESP_LOGW(TAG, "Some reTerminal IO is unavailable");
    int64_t rtc; s_rtc_ok = reterminal_io_rtc_read(&rtc);
    if (!gadget_clock_valid() && s_rtc_ok) gadget_clock_restore_utc(rtc);
    if (xTaskCreate(kid_task, "kid_board", 12288, NULL, 3, &s_task) != pdPASS) return false;
    gadget_clock_set_change_cb(clock_changed); if (gadget_clock_valid()) clock_changed(); return true;
}
