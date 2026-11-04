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

#include "sticky_notes.h"
#include "gadget_clock.h"
#include "gadget_events.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBOUNCE_US 3000000LL
#define RETRY_US 30000000LL
#define REQUEST_US 15000000LL
#define EVENT_MAX_WAIT_US 10000000LL
#define STORE_NAMESPACE "fridge_notes"
#define STORE_KEY "lists_v1"
typedef struct {
    char command[20], request_id[64];
    noise_ctrl_session_generation_t generation;
    cJSON *params;
    bool touch;
} job_t;
static sticky_notes_model_t *s_model;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_jobs;
static TaskHandle_t s_task;
static atomic_bool s_ready;
typedef void (*redraw_fn_t)(void);
static _Atomic(redraw_fn_t) s_redraw;
static uint32_t s_synced_revision;
static int s_synced_minute = -1;
static bool s_save_failed;
static int64_t s_changed_at, s_retry_at;
static struct {
    unsigned count;
    int64_t first_at, last_at;
    char summary[256];
} s_event;
static char *s_mirror;
static size_t s_offset;
static uint32_t s_mirror_revision;
static int64_t s_request, s_request_at;
#define RX_DONE 0x400U
#define RX_STATUS 0x3ffU
#define RX_TOKEN (~0x7ffU)
static atomic_uint s_rx_state;
static uint32_t s_rx_token;

static cJSON *failure(const char *message) {
    cJSON *root = cJSON_CreateObject(), *err = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(err, "code", "notes_unavailable");
    cJSON_AddStringToObject(err, "message", message);
    cJSON_AddItemToObject(root, "error", err);
    return root;
}

static void redraw(void) {
    redraw_fn_t callback = atomic_load(&s_redraw);
    if (callback) callback();
}

void sticky_notes_watch(void (*callback)(void)) {
    atomic_store(&s_redraw, callback);
}

bool sticky_notes_view(unsigned selected, sticky_notes_view_t *out) {
    if (!atomic_load(&s_ready) || !out || xSemaphoreTake(s_lock, pdMS_TO_TICKS(30)) != pdTRUE) return false;
    memset(out, 0, sizeof(*out));
    out->count = s_model->count;
    out->selected = selected < out->count ? selected : 0;
    for (unsigned i = 0; i < out->count; i++) {
        memcpy(out->names[i], s_model->lists[i].name, STICKY_NOTES_NAME);
        memcpy(out->titles[i], s_model->lists[i].title, STICKY_NOTES_TITLE);
    }
    if (out->count) out->list = s_model->lists[out->selected];
    out->revision = s_model->revision;
    out->pending = s_synced_revision != s_model->revision;
    out->synced_minute = s_synced_minute;
    out->save_failed = s_save_failed;
    xSemaphoreGive(s_lock);
    return true;
}

static bool store(const sticky_notes_model_t *model) {
    nvs_handle_t handle;
    if (nvs_open(STORE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    bool ok = nvs_set_blob(handle, STORE_KEY, model, sizeof(*model)) == ESP_OK
        && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

// Checks binary bounds before passing restored strings to the JSON validator.
// 在将恢复的字符串交给 JSON 校验器之前检查二进制数据边界。
static bool load(sticky_notes_model_t *model) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK) return false;
    size_t size = sizeof(*model);
    err = nvs_get_blob(handle, STORE_KEY, model, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { sticky_notes_model_init(model); return true; }
    if (err != ESP_OK || size != sizeof(*model) || model->count > STICKY_NOTES_LISTS) return false;
    for (unsigned l = 0; l < model->count; l++) {
        const sticky_notes_list_t *list = &model->lists[l];
        if (list->count > STICKY_NOTES_ITEMS || !memchr(list->name, 0, sizeof(list->name))
            || !memchr(list->title, 0, sizeof(list->title))) return false;
        for (unsigned i = 0; i < list->count; i++)
            if (*(const unsigned char *)&list->items[i].done > 1
                || !memchr(list->items[i].text, 0, STICKY_NOTES_TEXT)
                || !memchr(list->items[i].who, 0, STICKY_NOTES_WHO)) return false;
    }
    cJSON *json = sticky_notes_json(model, NULL);
    bool ok = json && sticky_notes_restore(model, json);
    cJSON_Delete(json);
    return ok;
}

// Collects local edits into one short chat sentence with a bounded burst window.
// 将本地修改合并为一句简短聊天消息，并限制连续操作的合并时长。
static void note_touch(const job_t *job, const sticky_notes_model_t *before,
                        const sticky_notes_model_t *after, int64_t now) {
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(job->params, "list"));
    for (unsigned l = 0; l < before->count; l++) {
        const sticky_notes_list_t *list = &before->lists[l];
        if (strcmp(list->name, name)) continue;
        if (!strcmp(job->command, "notes.clear_done")) {
            unsigned cleared = list->count - after->lists[l].count;
            snprintf(s_event.summary, sizeof(s_event.summary), "Cleared %u done items from %s.", cleared, list->title);
        } else {
            uint32_t id = (uint32_t)cJSON_GetObjectItemCaseSensitive(job->params, "id")->valuedouble;
            for (unsigned i = 0; i < list->count; i++) if (list->items[i].id == id)
                snprintf(s_event.summary, sizeof(s_event.summary), "%s \"%s\" on the %s list.",
                    after->lists[l].items[i].done ? "Ticked" : "Unticked", list->items[i].text, list->title);
        }
    }
    if (!s_event.count) s_event.first_at = now;
    s_event.count++;
    s_event.last_at = now;
}

// Saves a staged model before publishing it and answering the command.
// 先保存暂存清单，再发布状态并回复命令。
static void run_job(job_t *job) {
    sticky_notes_model_t *next = sticky_notes_model_alloc();
    cJSON *result;
    if (!next) result = failure("Out of memory");
    else {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        memcpy(next, s_model, sizeof(*next));
        xSemaphoreGive(s_lock);
        bool changed;
        int64_t epoch = gadget_clock_valid() ? gadget_clock_epoch() : 0;
        result = sticky_notes_apply(next, job->command, job->params, epoch, &changed);
        if (changed) {
            bool saved = store(next);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_save_failed = !saved;
            if (saved) {
                int64_t now = esp_timer_get_time();
                if (job->touch) note_touch(job, s_model, next, now);
                memcpy(s_model, next, sizeof(*s_model));
                s_changed_at = now;
                s_retry_at = 0;
            }
            xSemaphoreGive(s_lock);
            if (!saved) { cJSON_Delete(result); result = failure("Could not save lists; state unchanged"); }
            redraw();
        }
        free(next);
    }
    if (job->touch) cJSON_Delete(result);
    else noise_ctrl_send_command_result(job->generation, job->request_id, result);
    cJSON_Delete(job->params);
}

// Updates response status atomically for the current upload token.
// 为当前上传标记原子更新响应状态。
static void on_write(void *ctx, int status, const uint8_t *data, size_t len, bool end) {
    (void)data; (void)len;
    unsigned token = (unsigned)(uintptr_t)ctx;
    unsigned previous = atomic_load(&s_rx_state), next;
    do {
        if ((previous & RX_TOKEN) != token) return;
        next = previous;
        if (status) next = (next & ~RX_STATUS) | (status < 0 ? RX_STATUS : (unsigned)status & RX_STATUS);
        if (end || status < 0) next |= RX_DONE;
    } while (!atomic_compare_exchange_weak(&s_rx_state, &previous, next));
    if (next & RX_DONE) xTaskNotifyGive(s_task);
}

static void reset_mirror(int64_t now) {
    s_rx_token = (s_rx_token + 0x800U) & RX_TOKEN;
    atomic_store(&s_rx_state, s_rx_token);
    if (s_request) noise_ctrl_req_cancel(s_request);
    s_request = 0;
    cJSON_free(s_mirror); s_mirror = NULL;
    s_offset = 0;
    s_retry_at = now + RETRY_US;
}

// Advances one asynchronous file chunk and restarts overwrite after any failure.
// 推进一个异步文件分块，任一失败后从覆盖写入重新开始。
static void mirror_step(int64_t now) {
    if (s_request) {
        if (!(atomic_load(&s_rx_state) & RX_DONE)) {
            if (now - s_request_at >= REQUEST_US) reset_mirror(now);
            return;
        }
        unsigned status = atomic_load(&s_rx_state) & RX_STATUS;
        s_request = 0;
        if (status < 200 || status >= 300) { reset_mirror(now); return; }
        if (s_offset == strlen(s_mirror)) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_synced_revision = s_mirror_revision;
            s_synced_minute = gadget_clock_minutes_of_day();
            xSemaphoreGive(s_lock);
            cJSON_free(s_mirror); s_mirror = NULL;
            s_offset = 0;
            redraw();
        }
    }
    if (!s_mirror) {
        if (s_synced_revision == s_model->revision || now < s_retry_at
            || now - s_changed_at < DEBOUNCE_US) return;
        cJSON *json = sticky_notes_json(s_model, NULL);
        s_mirror = json ? cJSON_PrintUnformatted(json) : NULL;
        cJSON_Delete(json);
        if (!s_mirror) { s_retry_at = now + RETRY_US; return; }
        s_mirror_revision = s_model->revision;
    }
    size_t offset = s_offset;
    char *body = sticky_notes_write_body(s_mirror, &offset);
    if (!body) { reset_mirror(now); return; }
    s_rx_token = (s_rx_token + 0x800U) & RX_TOKEN;
    atomic_store(&s_rx_state, s_rx_token);
    const char *headers[] = {"Content-Type", "application/json", NULL};
    s_request = noise_ctrl_req_open("POST", "/fs/write", headers, false, on_write,
                                    (void *)(uintptr_t)s_rx_token);
    s_request_at = now;
    if (!s_request || !noise_ctrl_req_send(s_request, body, strlen(body), true, 0)) reset_mirror(now);
    else s_offset = offset;
    cJSON_free(body);
}

static void event_step(int64_t now) {
    if (!s_event.count || (now - s_event.last_at < DEBOUNCE_US
        && now - s_event.first_at < EVENT_MAX_WAIT_US)) return;
    char summary[256];
    if (s_event.count == 1) snprintf(summary, sizeof(summary), "%s", s_event.summary);
    else snprintf(summary, sizeof(summary), "Updated the fridge lists with %u touch changes.", s_event.count);
    if (gadget_events_post("fridge-notes", summary, NULL)) memset(&s_event, 0, sizeof(s_event));
    else { s_event.first_at = now; s_event.last_at = now; }
}

// Finds the next upload, retry or coalesced-event deadline.
// 找出下一次上传、重试或合并事件的到期时间。
static TickType_t next_wait(int64_t now) {
    int64_t deadline = INT64_MAX;
    if (s_request) deadline = s_request_at + REQUEST_US;
    else if (s_mirror) return 0;
    else if (s_synced_revision != s_model->revision) {
        deadline = s_changed_at + DEBOUNCE_US;
        if (s_retry_at > deadline) deadline = s_retry_at;
    }
    if (s_event.count) {
        int64_t event_at = s_event.last_at + DEBOUNCE_US;
        if (s_event.first_at + EVENT_MAX_WAIT_US < event_at) event_at = s_event.first_at + EVENT_MAX_WAIT_US;
        if (event_at < deadline) deadline = event_at;
    }
    if (deadline == INT64_MAX) return portMAX_DELAY;
    int64_t ms = deadline > now ? (deadline - now + 999) / 1000 : 1;
    return pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1;
}

static void worker(void *arg) {
    (void)arg;
    for (;;) {
        job_t job;
        for (unsigned i = 0; i < 4 && xQueueReceive(s_jobs, &job, 0) == pdTRUE; i++) run_job(&job);
        int64_t now = esp_timer_get_time();
        event_step(now);
        mirror_step(now);
        ulTaskNotifyTake(pdTRUE, next_wait(now));
    }
}

bool sticky_notes_init(void) {
    if (atomic_load(&s_ready)) return true;
    s_model = sticky_notes_model_alloc();
    if (!s_model) return false;
    sticky_notes_model_init(s_model);
    if (!load(s_model)) { free(s_model); s_model = NULL; return false; }
    s_lock = xSemaphoreCreateMutex();
    s_jobs = xQueueCreate(4, sizeof(job_t));
    if (!s_lock || !s_jobs || xTaskCreate(worker, "fridge_notes", 6144, NULL, 4, &s_task) != pdPASS) return false;
    atomic_store(&s_ready, true);
    redraw();
    return true;
}

cJSON *sticky_notes_command(const char *command, const cJSON *params,
                            const char *request_id, noise_ctrl_session_generation_t generation) {
    if (!atomic_load(&s_ready) || !sticky_notes_handles(command) || !request_id
        || !request_id[0] || strlen(request_id) >= 64 || !generation) return failure("Notes unavailable");
    cJSON *result = cJSON_CreateObject();
    if (!result || !cJSON_AddBoolToObject(result, "_async", true)) {
        cJSON_Delete(result); return failure("Out of memory");
    }
    job_t job = {.generation = generation};
    snprintf(job.command, sizeof(job.command), "%s", command);
    snprintf(job.request_id, sizeof(job.request_id), "%s", request_id);
    job.params = params ? cJSON_Duplicate(params, true) : cJSON_CreateObject();
    if (!job.params || xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        cJSON_Delete(job.params); cJSON_Delete(result);
        return failure("Notes queue busy; retry later");
    }
    xTaskNotifyGive(s_task);
    return result;
}

bool sticky_notes_touch(const char *list, uint32_t id, bool clear) {
    if (!atomic_load(&s_ready) || !list) return false;
    job_t job = {.touch = true};
    snprintf(job.command, sizeof(job.command), "%s", clear ? "notes.clear_done" : "notes.toggle");
    job.params = cJSON_CreateObject();
    if (!job.params) return false;
    if (!cJSON_AddStringToObject(job.params, "list", list)
        || (!clear && !cJSON_AddNumberToObject(job.params, "id", id))) {
        cJSON_Delete(job.params); return false;
    }
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) { cJSON_Delete(job.params); return false; }
    xTaskNotifyGive(s_task);
    return true;
}
