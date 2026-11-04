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

#include "care_board.h"
#include "care_store.h"
#include "care_memory.h"
#include "gadget_clock.h"
#include "gadget_events.h"
#include "reterminal_io.h"
#include "reterminal_sht4x.h"
#include "led_status.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "link.care";
typedef enum { JOB_UPDATE, JOB_STATE, JOB_CLEAR, JOB_KEY } job_kind_t;
typedef struct {
    job_kind_t kind;
    care_key_t key;
    cJSON *params;
    noise_ctrl_session_generation_t generation;
    char id[96];
} care_job_t;

static care_model_t *s_model;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_jobs;
static TaskHandle_t s_task;
static bool (*s_keys_ready)(void);
static atomic_bool s_clock_dirty;
static bool s_storage_ok = true, s_rtc_ok;
static bool s_redraw;
static bool s_confirmation_sound;
static int64_t s_store_retry_at;
static unsigned s_store_backoff_ms = 5000;

static cJSON *error_result(const char *code, const char *message) {
    cJSON *result = cJSON_CreateObject(), *error = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", false); cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message); cJSON_AddItemToObject(result, "error", error);
    return result;
}

static care_model_t *clone_model(void) {
    care_model_t *copy = care_memory_alloc(sizeof(*copy));
    if (copy) { xSemaphoreTake(s_lock, portMAX_DELAY); *copy = *s_model; xSemaphoreGive(s_lock); }
    return copy;
}

static const char *status_name(care_status_t status) {
    static const char *const names[] = {"pending", "due", "done", "missed"};
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "unknown";
}

// Logs state changes using reminder IDs and timestamps.
// 使用提醒编号和时间戳记录状态变化。
static void log_transitions(const care_model_t *old, const care_model_t *next) {
    for (unsigned i = 0; i < next->reminder_count; i++) {
        const care_reminder_t *item = &next->reminders[i], *before = NULL;
        for (unsigned j = 0; j < old->reminder_count; j++) {
            if (!strcmp(item->id, old->reminders[j].id)) { before = &old->reminders[j]; break; }
        }
        if (!before || before->status != item->status) ESP_LOGI(TAG,
            "reminder id=%s status=%s->%s due_at=%" PRId64 " done_at=%" PRId64,
            item->id, before ? status_name(before->status) : "new", status_name(item->status), item->due_at, item->done_at);
    }
    for (unsigned i = 0; i < old->reminder_count; i++) {
        bool found = false;
        for (unsigned j = 0; j < next->reminder_count; j++) found |= !strcmp(old->reminders[i].id, next->reminders[j].id);
        if (!found) ESP_LOGI(TAG, "reminder id=%s status=%s->removed", old->reminders[i].id, status_name(old->reminders[i].status));
    }
}

static void request_redraw(const char *reason) {
    s_redraw = true;
    ESP_LOGI(TAG, "redraw request reason=%s at=%" PRId64, reason, gadget_clock_epoch());
}

static void publish(care_model_t *model) {
    log_transitions(s_model, model);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    care_model_t *old = s_model; s_model = model;
    xSemaphoreGive(s_lock); free(old);
}

static bool post_event(const char *summary, const cJSON *data) {
    uint64_t seq = 0;
    bool ok = gadget_events_post_seq("Care Board", summary, data, &seq);
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "type"));
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "id"));
    ESP_LOGI(TAG, "event type=%s seq=%" PRIu64 " id=%s queued=%d at=%" PRId64,
        type ? type : "unknown", seq, id ? id : "-", ok, gadget_clock_epoch());
    return ok;
}
static void chime(bool gentle) { if (!gentle) s_confirmation_sound = true; reterminal_io_chime(gentle); }
static const care_hooks_t s_hooks = {.event = post_event, .chime = chime};

static cJSON *state_result(void) {
    cJSON *result = cJSON_CreateObject(), *payload = care_model_json(s_model);
    cJSON_AddBoolToObject(result, "ok", true); cJSON_AddItemToObject(result, "payload", payload);
    cJSON_AddBoolToObject(payload, "clock_valid", gadget_clock_valid());
    cJSON_AddNumberToObject(payload, "now_epoch", (double)gadget_clock_epoch());
    cJSON_AddNumberToObject(payload, "utc_offset_min", gadget_clock_offset_minutes());
    cJSON_AddBoolToObject(payload, "storage_ok", s_storage_ok); cJSON_AddBoolToObject(payload, "rtc_ok", s_rtc_ok);
    cJSON *readings = reterminal_sht4x_command();
    cJSON *sensors = cJSON_DetachItemFromObjectCaseSensitive(readings, "payload");
    if (sensors) cJSON_AddItemToObject(payload, "sensor", sensors);
    else cJSON_AddNullToObject(payload, "sensor");
    cJSON_Delete(readings); return result;
}

// Copies a short snapshot under the lock, then draws on the display task.
// 在锁内复制简短快照，随后在显示任务中绘图。
static void render(epaper_cards_canvas_t *canvas, void *ctx) {
    (void)ctx;
    care_model_t *model = clone_model();
    if (!model) return;
    cJSON *readings = reterminal_sht4x_command();
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(readings, "payload");
    cJSON *temp = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(payload, "temperature"), "value");
    cJSON *rh = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(payload, "humidity"), "value");
    care_model_render(canvas, model, gadget_clock_epoch(), gadget_clock_offset_minutes(),
        cJSON_IsNumber(temp) && cJSON_IsNumber(rh), temp ? temp->valuedouble : 0, rh ? rh->valuedouble : 0);
    cJSON_Delete(readings); free(model);
}

void care_board_notify(void) { if (s_task) xTaskNotifyGive(s_task); }

static void clock_changed(void) { atomic_store(&s_clock_dirty, true); if (s_task) xTaskNotifyGive(s_task); }
static void key_received(reterminal_key_t key) { care_board_key((care_key_t)key); }
static bool keys_ready(void) { return s_task && s_keys_ready && s_keys_ready(); }

bool care_board_key(care_key_t key) {
    if (!keys_ready() || key < 0 || key >= CARE_KEYS) return false;
    care_job_t job = {.kind = JOB_KEY, .key = key};
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) return false;
    xTaskNotifyGive(s_task); return true;
}

// Commands retain their own parameters and reply through the worker task.
// 命令持有独立参数副本，并通过后台任务回复。
cJSON *care_board_command(const char *command, const cJSON *params,
                           const char *request_id, noise_ctrl_session_generation_t generation) {
    if (!s_task) return error_result("unavailable", "Care Board is not ready");
    if (!request_id || strlen(request_id) >= sizeof(((care_job_t *)0)->id)) return error_result("invalid_params", "invalid request id");
    care_job_t job = {.generation = generation};
    if (!strcmp(command, "care.update")) {
        if (!cJSON_IsObject(params)) return error_result("invalid_params", "care.update takes a patch object");
        job.kind = JOB_UPDATE; job.params = cJSON_Duplicate(params, true);
        if (!job.params) return error_result("out_of_memory", "could not copy care patch");
    } else if (!strcmp(command, "care.state") || !strcmp(command, "care.clear")) {
        if (params && (!cJSON_IsObject(params) || params->child)) return error_result("invalid_params", "this care command takes no parameters");
        job.kind = !strcmp(command, "care.state") ? JOB_STATE : JOB_CLEAR;
    } else return error_result("unsupported", "unknown care command");
    strcpy(job.id, request_id);
    cJSON *result = cJSON_CreateObject();
    if (!result) { cJSON_Delete(job.params); return NULL; }
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        cJSON_Delete(job.params); cJSON_Delete(result); return error_result("busy", "Care Board command queue is full");
    }
    cJSON_AddBoolToObject(result, "_async", true); xTaskNotifyGive(s_task); return result;
}

static void process_job(care_job_t *job) {
    cJSON *result = NULL;
    if (job->kind == JOB_STATE) result = state_result();
    else {
        care_model_t *candidate = clone_model();
        if (!candidate) result = error_result("out_of_memory", "could not copy care state");
        else if (job->kind == JOB_KEY) {
            if (keys_ready()) {
                int oldest = care_model_oldest(candidate);
                bool unread = false;
                for (unsigned i = 0; i < candidate->message_count; i++) unread |= !candidate->messages[i].read;
                const char *action = job->key == CARE_LEFT ? "checkin" : job->key == CARE_RIGHT ? "call_request"
                    : oldest >= 0 ? "ack" : unread ? "read" : "nothing";
                const char *id = job->key == CARE_GREEN && oldest >= 0 ? candidate->reminders[oldest].id : "-";
                bool accepted = care_model_key(candidate, job->key, gadget_clock_epoch(),
                    gadget_clock_offset_minutes(), esp_timer_get_time(), &s_hooks);
                ESP_LOGI(TAG, "key=%d action=%s id=%s accepted=%d at=%" PRId64,
                    (int)job->key, action, id, accepted, gadget_clock_epoch());
                if (accepted || strcmp(candidate->footer, s_model->footer)) {
                    s_storage_ok = care_store_save(candidate); publish(candidate); candidate = NULL; request_redraw("key");
                }
            }
            free(candidate);
        } else if (job->kind == JOB_CLEAR) {
            candidate->active = false;
            s_storage_ok = care_store_save(candidate);
            if (!s_storage_ok) { free(candidate); result = error_result("storage_failed", "could not save care state"); }
            else { publish(candidate); led_status_show_animation(); s_redraw = false; ESP_LOGI(TAG, "redraw request reason=clear"); result = state_result(); }
        } else {
            bool new_message; const char *error;
            if (!care_model_patch(candidate, job->params, gadget_clock_epoch(), gadget_clock_offset_minutes(), &new_message, &error)) {
                free(candidate); result = error_result("invalid_params", error);
            } else if (!(s_storage_ok = care_store_save(candidate))) {
                free(candidate); result = error_result("storage_failed", "could not save care state");
            } else {
                const cJSON *epoch = cJSON_GetObjectItemCaseSensitive(job->params, "now_epoch");
                const cJSON *offset = cJSON_GetObjectItemCaseSensitive(job->params, "utc_offset_min");
                if (epoch && !gadget_clock_set((int64_t)epoch->valuedouble, (int)offset->valuedouble)) {
                    s_storage_ok = care_store_save(s_model); free(candidate);
                    result = error_result("clock_failed", "could not set care clock");
                } else {
                    publish(candidate); request_redraw("update");
                    if (new_message) chime(false);
                    result = state_result();
                }
            }
        }
    }
    cJSON_Delete(job->params);
    if (job->kind != JOB_KEY) noise_ctrl_send_command_result(job->generation, job->id, result);
}

static bool visible_changed(const care_model_t *old, const care_model_t *next) {
    if (old->day != next->day || old->schedule_offset != next->schedule_offset
        || old->reminder_count != next->reminder_count) return true;
    for (unsigned i = 0; i < next->reminder_count; i++) if (old->reminders[i].status != next->reminders[i].status) return true;
    return false;
}

// Converts long deadline intervals to RTOS ticks using 64-bit arithmetic.
// 使用 64 位运算将较长的截止间隔转换为任务时钟计数。
static TickType_t deadline_ticks(int64_t milliseconds) {
    if (milliseconds < 0) return portMAX_DELAY;
    uint64_t ticks = ((uint64_t)milliseconds * configTICK_RATE_HZ + 999) / 1000;
    return (TickType_t)(ticks ? ticks : 1);
}

// Sleeps until a command, a key, a clock change or the next reminder deadline.
// 休眠至命令、按键、时钟变化或下一次提醒截止点。
static void care_task(void *arg) {
    (void)arg;
    bool was_due = false;
    for (;;) {
        care_job_t job;
        s_confirmation_sound = false;
        for (unsigned i = 0; i < 8 && xQueueReceive(s_jobs, &job, 0) == pdTRUE; i++) process_job(&job);
        if (atomic_exchange(&s_clock_dirty, false) && gadget_clock_valid()) {
            s_rtc_ok = reterminal_io_rtc_write(gadget_clock_epoch());
            if (!s_rtc_ok) ESP_LOGW(TAG, "RTC sync unavailable");
        }
        care_model_t *candidate = clone_model();
        int64_t now = gadget_clock_epoch(), next = 0;
        if (candidate) {
            bool changed;
            next = care_model_step(candidate, now, gadget_clock_offset_minutes(), &s_hooks, &changed);
            if (changed) {
                if (visible_changed(s_model, candidate)) request_redraw("schedule");
                s_storage_ok = care_store_save(candidate); publish(candidate);
            } else free(candidate);
        } else next = now + 1;
        if (!s_storage_ok && esp_timer_get_time() >= s_store_retry_at) {
            s_storage_ok = care_store_save(s_model);
            s_store_retry_at = esp_timer_get_time() + s_store_backoff_ms * 1000LL;
            if (!s_storage_ok && s_store_backoff_ms < 60000) {
                s_store_backoff_ms = s_store_backoff_ms > 30000 ? 60000 : s_store_backoff_ms * 2;
            }
        }
        if (s_storage_ok) s_store_backoff_ms = 5000;
        bool due = false;
        for (unsigned i = 0; i < s_model->reminder_count; i++) due |= s_model->reminders[i].status == CARE_DUE;
        reterminal_io_due(due);
        if (was_due && !due && !s_confirmation_sound) reterminal_io_silence();
        was_due = due;
        bool ready = keys_ready();
        if (s_redraw && s_model->active && ready) s_redraw = !epaper_cards_show(render, NULL);
        if (!s_model->active) s_redraw = false;
        int64_t wait_ms = next > 0 ? (next > now ? (next - now) * 1000 : 1) : -1;
        if (s_redraw && ready && (wait_ms < 0 || wait_ms > 1000)) wait_ms = 1000;
        if (!s_storage_ok) {
            int64_t retry_ms = (s_store_retry_at - esp_timer_get_time()) / 1000 + 1;
            if (retry_ms < 1) retry_ms = 1;
            if (wait_ms < 0 || wait_ms > retry_ms) wait_ms = retry_ms;
        }
        if (uxQueueMessagesWaiting(s_jobs)) wait_ms = 1;
        if (wait_ms > 86400000) wait_ms = 86400000;
        ulTaskNotifyTake(pdTRUE, deadline_ticks(wait_ms));
    }
}

bool care_board_init(bool (*ready)(void)) {
    if (s_task || !ready) return false;
    s_keys_ready = ready;
    s_lock = xSemaphoreCreateMutex(); s_jobs = xQueueCreate(8, sizeof(care_job_t));
    s_model = care_memory_alloc(sizeof(*s_model));
    if (!s_lock || !s_jobs || !s_model) return false;
    care_model_init(s_model); care_store_load(s_model);
    if (s_model->active) request_redraw("restore");
    if (!reterminal_io_init(keys_ready, key_received)) ESP_LOGW(TAG, "some reTerminal IO is unavailable");
    int64_t rtc;
    s_rtc_ok = reterminal_io_rtc_read(&rtc);
    if (!gadget_clock_valid() && s_rtc_ok) gadget_clock_restore_utc(rtc);
    if (xTaskCreate(care_task, "care_board", 8192, NULL, 3, &s_task) != pdPASS) return false;
    gadget_clock_set_change_cb(clock_changed);
    if (gadget_clock_valid()) clock_changed();
    return true;
}
