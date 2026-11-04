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

#include "air_guardian.h"
#include "air_guardian_core.h"
#include "config_store.h"
#include "gadget_clock.h"
#include "gadget_events.h"
#include "led_status.h"
#include "sensecap_sensors.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define AIR_SETTINGS_KEY "air_settings"
#define AIR_REQUEST_BYTES 96
#define AIR_ALERTS_KEY "air_alerts"

typedef struct {
    char command[24], request_id[AIR_REQUEST_BYTES];
    cJSON *params;
    noise_ctrl_session_generation_t generation;
    bool cycle;
} air_request_t;

static air_core_t s_core;
static QueueHandle_t s_requests;
static uint16_t *s_pixels;
static bool s_alerts_dirty;
static const char *TAG = "link.air";

static int64_t now_s(void) { return esp_timer_get_time() / 1000000; }
static int64_t epoch(void) { return gadget_clock_valid() ? gadget_clock_epoch() : 0; }

static bool post(const char *summary, const cJSON *data, void *ctx) {
    (void)ctx;
    bool ok = gadget_events_post("Air Guardian", summary, data);
    s_alerts_dirty |= ok;
    return ok;
}

static bool save(const air_settings_t *s, void *ctx) {
    (void)ctx;
    cJSON *p = air_settings_json(s);
    if (!p || !cJSON_AddStringToObject(p, "view", air_view_names[s->view])) {
        cJSON_Delete(p);
        return false;
    }
    char *json = cJSON_PrintUnformatted(p);
    bool ok = json && config_set_str(AIR_SETTINGS_KEY, json);
    cJSON_free(json);
    cJSON_Delete(p);
    return ok;
}

// Restores one atomic settings record through the command validators.
// 通过命令校验器恢复单条原子保存的设置记录。
static void restore(void) {
    char json[512];
    if (!config_get_str(AIR_SETTINGS_KEY, json, sizeof(json))) return;
    cJSON *p = cJSON_Parse(json);
    cJSON *v = cJSON_DetachItemFromObjectCaseSensitive(p, "view");
    air_settings_t settings, full;
    if (p && v) {
        cJSON *vp = cJSON_CreateObject();
        cJSON_AddItemToObject(vp, "view", v);
        v = NULL;
        if (air_settings_parse(&settings, &s_core.settings, p, false)
            && air_settings_parse(&full, &settings, vp, true)) s_core.settings = full;
        cJSON_Delete(vp);
    }
    cJSON_Delete(v);
    cJSON_Delete(p);
}

// Stores the latest alert per limit and restores cooldowns against UTC when available.
// 保存每个阈值的最近提醒，并在 UTC 有效时恢复对应的冷却时间。
static bool save_alerts(void) {
    cJSON *p = cJSON_CreateArray();
    if (!p) return false;
    int64_t utc = epoch(), now = now_s();
    for (int i = 0; i < AIR_LIMITS; i++) {
        const air_alarm_t *a = &s_core.alarms[i];
        cJSON *item = cJSON_CreateObject();
        bool complete = item
            && cJSON_AddBoolToObject(item, "active", a->active)
            && cJSON_AddBoolToObject(item, "announced", a->announced)
            && cJSON_AddBoolToObject(item, "sent", a->sent_before)
            && cJSON_AddNumberToObject(item, "last_sent_epoch", utc && a->sent_before ? (double)(utc - now + a->last_sent_s) : 0)
            && cJSON_AddNumberToObject(item, "time", (double)a->last_event_time)
            && cJSON_AddBoolToObject(item, "epoch", a->last_event_epoch)
            && cJSON_AddStringToObject(item, "summary", a->last_message);
        if (!complete || !cJSON_AddItemToArray(p, item)) {
            cJSON_Delete(item);
            cJSON_Delete(p);
            return false;
        }
    }
    char *json = cJSON_PrintUnformatted(p);
    bool ok = json && config_set_str(AIR_ALERTS_KEY, json);
    cJSON_free(json);
    cJSON_Delete(p);
    return ok;
}

static void restore_alerts(void) {
    char *json = malloc(3072);
    air_alarm_t *restored = calloc(AIR_LIMITS, sizeof(*restored));
    cJSON *p = NULL;
    if (!json || !restored || !config_get_str(AIR_ALERTS_KEY, json, 3072)) goto done;
    p = cJSON_Parse(json);
    if (!cJSON_IsArray(p) || cJSON_GetArraySize(p) != AIR_LIMITS) goto done;
    int64_t utc = epoch(), now = now_s();
    for (int i = 0; i < AIR_LIMITS; i++) {
        cJSON *item = cJSON_GetArrayItem(p, i);
        cJSON *active = cJSON_GetObjectItemCaseSensitive(item, "active");
        cJSON *announced = cJSON_GetObjectItemCaseSensitive(item, "announced");
        cJSON *sent = cJSON_GetObjectItemCaseSensitive(item, "sent");
        cJSON *last_sent = cJSON_GetObjectItemCaseSensitive(item, "last_sent_epoch");
        cJSON *time = cJSON_GetObjectItemCaseSensitive(item, "time");
        cJSON *basis = cJSON_GetObjectItemCaseSensitive(item, "epoch");
        cJSON *summary = cJSON_GetObjectItemCaseSensitive(item, "summary");
        if (!cJSON_IsBool(active) || !cJSON_IsBool(announced) || !cJSON_IsBool(sent)
            || !cJSON_IsBool(basis) || !cJSON_IsNumber(last_sent) || !cJSON_IsNumber(time)
            || !isfinite(last_sent->valuedouble) || last_sent->valuedouble < 0 || last_sent->valuedouble > 4102444799LL
            || !isfinite(time->valuedouble) || time->valuedouble < 0 || time->valuedouble > 4102444799LL
            || !cJSON_IsString(summary) || !summary->valuestring || strlen(summary->valuestring) >= sizeof(restored[i].last_message)) goto done;
        air_alarm_t *a = &restored[i];
        a->active = cJSON_IsTrue(active); a->announced = cJSON_IsTrue(announced);
        a->sent_before = cJSON_IsTrue(sent); a->last_event_epoch = cJSON_IsTrue(basis);
        a->last_event_time = (int64_t)time->valuedouble;
        int64_t age = utc > 0 && last_sent->valuedouble > 0 ? utc - (int64_t)last_sent->valuedouble : 0;
        if (age < 0) age = 0;
        if (age > AIR_COOLDOWN_S) age = AIR_COOLDOWN_S;
        a->last_sent_s = now - age;
        strcpy(a->last_message, summary->valuestring);
    }
    memcpy(s_core.alarms, restored, sizeof(s_core.alarms));
done:
    cJSON_Delete(p);
    free(restored);
    free(json);
}

static void observe(void) {
    cJSON *result = sensecap_sensors_command();
    const cJSON *payload = cJSON_GetObjectItemCaseSensitive(result, "payload");
    float values[AIR_METRICS] = {0};
    uint8_t valid = 0;
    for (int i = 0; i < AIR_METRICS; i++) {
        const cJSON *reading = cJSON_GetObjectItemCaseSensitive(payload, air_metric_names[i]);
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(reading, "value");
        if (cJSON_IsNumber(v)) { values[i] = (float)v->valuedouble; valid |= 1u << i; }
    }
    air_core_observe(&s_core, values, valid, now_s());
    cJSON_Delete(result);
}

// Owns history, NVS, event production and coalesced native LCD redraws.
// 独占历史、NVS、事件生成以及合并后的原生 LCD 重绘。
static void air_task(void *arg) {
    (void)arg;
    card_rgb565_t canvas;
    card_rgb565_canvas(&canvas, s_pixels, 480 * 480 * 2, 480, 480);
    int64_t observed = -5, rendered = -5;
    bool dirty = true;
    for (;;) {
        air_request_t request;
        bool received = xQueueReceive(s_requests, &request, pdMS_TO_TICKS(1000)) == pdTRUE;
        int64_t now = now_s();
        if (now - observed >= 5) { observe(); observed = now; dirty = true; }
        if (received) {
            if (request.cycle) {
                cJSON *params = cJSON_CreateObject();
                cJSON_AddStringToObject(params, "view", air_view_names[(s_core.settings.view + 1) % 4]);
                cJSON *r = air_core_command(&s_core, "air.set_view", params, now, epoch(),
                                            gadget_clock_minutes_of_day(), save, NULL);
                cJSON *ok = cJSON_GetObjectItemCaseSensitive(r, "ok");
                if (!cJSON_IsTrue(ok)) ESP_LOGW(TAG, "view could not be saved");
                cJSON_Delete(r);
                cJSON_Delete(params);
            } else {
                cJSON *r = air_core_command(&s_core, request.command, request.params, now, epoch(),
                                            gadget_clock_minutes_of_day(), save, NULL);
                noise_ctrl_send_command_result(request.generation, request.request_id, r);
            }
            cJSON_Delete(request.params);
            dirty = true;
        }
        air_core_evaluate(&s_core, now, epoch(), post, NULL);
        if (s_alerts_dirty && save_alerts()) s_alerts_dirty = false;
        bool active = false;
        for (int i = 0; i < AIR_LIMITS; i++) active |= s_core.alarms[i].active;
        if (dirty || now - rendered >= (active ? 1 : 5)) {
            air_guardian_render(&canvas, &s_core, now, gadget_clock_minutes_of_day(), (now & 1) != 0);
            led_status_air_draw(s_pixels);
            dirty = false;
            rendered = now;
        }
    }
}

bool air_guardian_init(void) {
    air_sample_t *history = heap_caps_malloc(sizeof(*history) * AIR_MINUTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_pixels = heap_caps_malloc(480 * 480 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_requests = xQueueCreate(4, sizeof(air_request_t));
    if (!history || !s_pixels || !s_requests) goto fail;
    air_core_init(&s_core, history);
    restore();
    restore_alerts();
    if (xTaskCreate(air_task, "air_guardian", 8192, NULL, 2, NULL) != pdPASS) goto fail;
    return true;
fail:
    free(history);
    free(s_pixels);
    s_pixels = NULL;
    if (s_requests) vQueueDelete(s_requests);
    s_requests = NULL;
    ESP_LOGE(TAG, "Air Guardian allocation failed");
    return false;
}

cJSON *air_guardian_command(const char *command, const cJSON *params, const char *request_id,
                            noise_ctrl_session_generation_t generation) {
    air_request_t request = {.generation = generation};
    const char *error = NULL;
    if (!command || strlen(command) >= sizeof(request.command) || !request_id
        || strlen(request_id) >= sizeof(request.request_id)) error = "Invalid request metadata";
    else if (!s_requests) error = "Air Guardian is unavailable";
    else if (params && !(request.params = cJSON_Duplicate(params, true))) error = "Air Guardian allocation failed";
    else {
        strcpy(request.command, command);
        strcpy(request.request_id, request_id);
        if (xQueueSend(s_requests, &request, 0) != pdTRUE) error = "Air Guardian is busy; retry shortly";
    }
    cJSON *r = cJSON_CreateObject();
    if (!error) cJSON_AddBoolToObject(r, "_async", true);
    else {
        cJSON_Delete(request.params);
        cJSON_AddBoolToObject(r, "ok", false);
        cJSON *e = cJSON_AddObjectToObject(r, "error");
        cJSON_AddStringToObject(e, "code", "unavailable");
        cJSON_AddStringToObject(e, "message", error);
    }
    return r;
}

void air_guardian_cycle_view(void) {
    air_request_t request = {.cycle = true};
    if (s_requests) (void)xQueueSend(s_requests, &request, 0);
}
