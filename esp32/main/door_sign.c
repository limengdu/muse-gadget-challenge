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

#include "door_sign.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_store.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gadget_clock.h"
#include "gadget_events.h"

#define SIGN_KEY "door_sign"
#define REQUEST_BYTES 129

static const char *TAG = "link.door_sign";
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_jobs;
static door_sign_snapshot_t s_view;
static door_sign_model_t s_current;
static int64_t s_last_tap_us = -1;
static char s_expiry_event[256];

typedef enum { SIGN_SET, SIGN_CLEAR, SIGN_REPLY } sign_job_kind_t;
typedef struct {
    sign_job_kind_t kind;
    door_sign_model_t sign;
    char request_id[REQUEST_BYTES];
    noise_ctrl_session_generation_t generation;
    unsigned reply;
    uint32_t revision;
    int64_t tapped_us;
    door_sign_time_t tapped_at;
} sign_job_t;

static door_sign_time_t local_time(void)
{
    struct tm local;
    door_sign_time_t now = {.epoch = gadget_clock_epoch()};
    now.valid = gadget_clock_local(&local);
    if (now.valid) now.local_second = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    return now;
}

static cJSON *failure(const char *code, const char *message)
{
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", false);
    cJSON *error = cJSON_AddObjectToObject(result, "error");
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    return result;
}

static cJSON *result_for(const door_sign_model_t *sign)
{
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON *payload = door_sign_json(sign);
    cJSON_AddBoolToObject(payload, "active", sign->active);
    cJSON_AddItemToObject(result, "payload", payload);
    return result;
}

static bool save(const door_sign_model_t *sign)
{
    char *stored = door_sign_encode(sign);
    bool ok = stored && strlen(stored) < DOOR_SIGN_STORAGE_BYTES && config_set_str(SIGN_KEY, stored);
    cJSON_free(stored);
    return ok;
}

// Publish only durable state; the UI reads a short locked copy.
// 仅发布已保存的状态；界面通过短时间加锁读取副本。
static void publish(const door_sign_model_t *sign)
{
    s_current = *sign;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_view.sign = *sign;
    s_view.revision++;
    s_view.sent_us = -1;
    s_view.sent_reply = -1;
    xSemaphoreGive(s_lock);
}

bool door_sign_snapshot(door_sign_snapshot_t *out)
{
    if (!out || !s_lock || xSemaphoreTake(s_lock, 0) != pdTRUE) return false;
    *out = s_view;
    xSemaphoreGive(s_lock);
    door_sign_time_t now = local_time();
    out->clock_valid = now.valid;
    out->epoch = now.epoch;
    out->local_second = now.local_second;
    return true;
}

bool door_sign_reply(unsigned index, uint32_t revision)
{
    if (!s_jobs || index >= DOOR_SIGN_REPLIES) return false;
    sign_job_t *job = calloc(1, sizeof(*job));
    if (!job) return false;
    job->kind = SIGN_REPLY;
    job->reply = index;
    job->revision = revision;
    job->tapped_us = esp_timer_get_time();
    job->tapped_at = local_time();
    if (xQueueSend(s_jobs, &job, 0) == pdTRUE) return true;
    free(job);
    return false;
}

// Apply one sign command or deliver a tap for the matching screen revision.
// 执行一条门牌命令，或发送与屏幕版本匹配的点击消息。
static void process_job(sign_job_t *job)
{
    if (job->kind == SIGN_REPLY) {
        if (job->revision != s_view.revision) return;
        char summary[256];
        int64_t last = s_last_tap_us;
        if (!door_sign_tap(&s_current, job->reply, job->tapped_us, &last, job->tapped_at, summary)) return;
        cJSON *data = cJSON_CreateObject();
        if (!data) return;
        cJSON_AddStringToObject(data, "kind", "reply");
        cJSON_AddStringToObject(data, "owner", s_current.owner);
        cJSON_AddStringToObject(data, "reply", s_current.replies[job->reply]);
        cJSON_AddStringToObject(data, "state", door_sign_state_name(s_current.state));
        bool posted = gadget_events_post("door-sign", summary, data);
        cJSON_Delete(data);
        if (!posted) {
            ESP_LOGW(TAG, "reply event queue busy");
            return;
        }
        s_last_tap_us = last;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_view.sent_us = esp_timer_get_time();
        s_view.sent_reply = (int)job->reply;
        xSemaphoreGive(s_lock);
        return;
    }
    door_sign_model_t next = job->kind == SIGN_CLEAR ? s_current : job->sign;
    if (job->kind == SIGN_CLEAR) next.active = false;
    bool ok = save(&next);
    if (ok) publish(&next);
    noise_ctrl_send_command_result(job->generation, job->request_id,
        ok ? result_for(&next) : failure("storage_failed", "Door sign could not be saved"));
}

// Expire once after clock sync, persist Free, and retry its event until queued.
// 时钟同步后只执行一次到期转换，保存空闲状态，并重试事件直至入队。
static void check_expiry(void)
{
    door_sign_model_t next = s_current;
    if (!s_expiry_event[0] && door_sign_expire(&next, local_time()) && save(&next)) {
        publish(&next);
        snprintf(s_expiry_event, sizeof(s_expiry_event), "%s's door sign switched back to \"Come in\".", next.owner);
    }
    if (s_expiry_event[0]) {
        cJSON *data = cJSON_CreateObject();
        if (!data) return;
        cJSON_AddStringToObject(data, "kind", "expired");
        if (gadget_events_post("door-sign", s_expiry_event, data)) s_expiry_event[0] = '\0';
        cJSON_Delete(data);
    }
}

// Wait for jobs, with a timed wake while a deadline or expiry event is pending.
// 等待任务，在存在截止时间或待发送到期事件时定时唤醒。
static void sign_task(void *arg)
{
    (void)arg;
    for (;;) {
        check_expiry();
        TickType_t wait = (s_current.active && s_current.until) || s_expiry_event[0]
            ? pdMS_TO_TICKS(1000) : portMAX_DELAY;
        sign_job_t *job;
        if (xQueueReceive(s_jobs, &job, wait) == pdTRUE) {
            process_job(job);
            free(job);
        }
    }
}

bool door_sign_init(void)
{
    if (s_jobs) return true;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return false;
    door_sign_defaults(&s_current, DOOR_SIGN_FREE);
    s_current.active = false;
    char *stored = malloc(DOOR_SIGN_STORAGE_BYTES);
    if (!stored) return false;
    if (config_get_str(SIGN_KEY, stored, DOOR_SIGN_STORAGE_BYTES)
        && !door_sign_decode(stored, &s_current)) ESP_LOGW(TAG, "stored sign invalid");
    free(stored);
    publish(&s_current);
    s_jobs = xQueueCreate(8, sizeof(sign_job_t *));
    if (!s_jobs) return false;
    if (xTaskCreate(sign_task, "door_sign", 6144, NULL, 3, NULL) != pdPASS) {
        vQueueDelete(s_jobs);
        s_jobs = NULL;
        return false;
    }
    return true;
}

// Validate requests and queue state changes for the storage worker.
// 校验请求，并将状态修改送入存储工作任务。
cJSON *door_sign_command(const char *command, const cJSON *params,
                         const char *request_id, noise_ctrl_session_generation_t generation)
{
    if (!command || (strcmp(command, "sign.set") && strcmp(command, "sign.get") && strcmp(command, "sign.clear")))
        return failure("unsupported", "Unknown sign command");
    bool setting = !strcmp(command, "sign.set");
    if (!setting && params && (!cJSON_IsObject(params) || params->child))
        return failure("invalid_params", "This sign command takes no parameters");
    if (!s_jobs) return failure("unavailable", "Door sign service is unavailable");
    if (!strcmp(command, "sign.get")) {
        door_sign_snapshot_t view;
        return door_sign_snapshot(&view) ? result_for(&view.sign) : failure("busy", "Try sign.get again");
    }
    if (!request_id || strlen(request_id) >= REQUEST_BYTES)
        return failure("invalid_params", "Invalid request ID");
    sign_job_t *job = calloc(1, sizeof(*job));
    if (!job) return failure("no_memory", "Door sign allocation failed");
    const char *error = NULL;
    if (setting && !door_sign_parse(params, local_time(), &job->sign, &error)) {
        free(job);
        return failure("invalid_params", error);
    }
    job->kind = setting ? SIGN_SET : SIGN_CLEAR;
    strcpy(job->request_id, request_id);
    job->generation = generation;
    cJSON *result = cJSON_CreateObject();
    if (!result || !cJSON_AddBoolToObject(result, "_async", true)) {
        cJSON_Delete(result);
        free(job);
        return failure("no_memory", "Door sign allocation failed");
    }
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        cJSON_Delete(result);
        free(job);
        return failure("busy", "Door sign queue is full");
    }
    return result;
}

static void parameter(cJSON *params, const char *name, const char *type, const char *description)
{
    cJSON *param = cJSON_AddObjectToObject(params, name);
    cJSON_AddStringToObject(param, "type", type);
    cJSON_AddStringToObject(param, "description", description);
}

static void command(cJSON *commands, const char *name, const char *description, cJSON *required, cJSON *optional)
{
    cJSON *cmd = cJSON_AddObjectToObject(commands, name);
    cJSON_AddStringToObject(cmd, "description", description);
    cJSON_AddItemToObject(cmd, "required", required ? required : cJSON_CreateObject());
    cJSON_AddItemToObject(cmd, "optional", optional ? optional : cJSON_CreateObject());
}

void door_sign_register(cJSON *commands)
{
    cJSON *required = cJSON_CreateObject();
    parameter(required, "state", "string", "meeting|focus|free|away|custom");
    cJSON *optional = cJSON_CreateObject();
    parameter(optional, "title", "string", "Prefer one word, <=10 characters; omit for state word. Accepts 1-40 UTF-8 bytes");
    parameter(optional, "subtitle", "string", "Optional detail, up to 80 UTF-8 bytes; not shown on narrow screens");
    parameter(optional, "until", "string|integer", "Next local HH:MM or future UTC epoch; clock.set first");
    parameter(optional, "color", "string", "Custom only: #RRGGBB");
    parameter(optional, "owner", "string", "Door owner, 1-24 UTF-8 bytes; default Dad");
    parameter(optional, "replies", "array", "0-3 distinct labels; prefer <=12 characters: Dinner, Need you, Parcel, Call me. Accepts 1-28 UTF-8 bytes each");
    command(commands, "sign.set", "Replace and save the door sign. Use short status and reply words. Taps send door-sign events to chat.", required, optional);
    command(commands, "sign.get", "Read the saved door sign and active flag.", NULL, NULL);
    command(commands, "sign.clear", "Save the sign as inactive and return to the Muse face.", NULL, NULL);
}
