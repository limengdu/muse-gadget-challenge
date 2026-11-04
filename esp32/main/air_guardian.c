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
#include "led_status.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

#define AIR_STATE_KEY "outdoor_air"
#define AIR_REQUEST_BYTES 96

typedef struct {
    char command[24], request_id[AIR_REQUEST_BYTES];
    cJSON *params;
    noise_ctrl_session_generation_t generation;
    bool cycle;
} air_request_t;

static air_core_t s_core;
static QueueHandle_t s_requests;
static uint16_t *s_pixels;
static const char *TAG = "link.air";

static int64_t now_s(void) { return esp_timer_get_time() / 1000000; }
static int64_t epoch(void) { return gadget_clock_valid() ? gadget_clock_epoch() : 0; }

static bool save(const air_core_t *state, void *ctx) {
    (void)ctx;
    cJSON *p = air_core_json(state);
    char *json = p ? cJSON_PrintUnformatted(p) : NULL;
    bool ok = json && config_set_str(AIR_STATE_KEY, json);
    cJSON_free(json);
    cJSON_Delete(p);
    return ok;
}

// Restores the last complete outdoor snapshot and selected view from NVS.
// 从 NVS 恢复上一次完整的户外空气快照和所选视图。
static void restore(void) {
    char json[1024];
    if (!config_get_str(AIR_STATE_KEY, json, sizeof(json))) return;
    cJSON *p = cJSON_ParseWithOpts(json, NULL, true);
    if (!air_core_restore(&s_core, p)) ESP_LOGW(TAG, "saved outdoor snapshot is invalid");
    cJSON_Delete(p);
}

// Owns snapshot updates, NVS writes and native LCD redraws on a single worker.
// 在单个后台任务中处理快照更新、NVS 写入和原生 LCD 重绘。
static void air_task(void *arg) {
    (void)arg;
    card_rgb565_t canvas;
    card_rgb565_canvas(&canvas, s_pixels, 480 * 480 * 2, 480, 480);
    int64_t rendered = -5;
    bool dirty = true;
    for (;;) {
        air_request_t request;
        bool received = xQueueReceive(s_requests, &request, pdMS_TO_TICKS(5000)) == pdTRUE;
        int64_t now = now_s();
        if (received) {
            if (request.cycle) {
                cJSON *params = cJSON_CreateObject();
                cJSON_AddStringToObject(params, "view", air_view_names[(s_core.view + 1) % AIR_VIEW_COUNT]);
                cJSON *r = air_core_command(&s_core, "air.set_view", params, epoch(), save, NULL);
                cJSON *ok = cJSON_GetObjectItemCaseSensitive(r, "ok");
                if (!cJSON_IsTrue(ok)) ESP_LOGW(TAG, "view could not be saved");
                cJSON_Delete(r);
                cJSON_Delete(params);
            } else {
                cJSON *r = air_core_command(&s_core, request.command, request.params, epoch(), save, NULL);
                noise_ctrl_send_command_result(request.generation, request.request_id, r);
            }
            cJSON_Delete(request.params);
            dirty = true;
        }
        if (dirty || now - rendered >= 5) {
            air_guardian_render(&canvas, &s_core, epoch(), gadget_clock_minutes_of_day());
            led_status_air_draw(s_pixels);
            dirty = false;
            rendered = now;
        }
    }
}

bool air_guardian_init(void) {
    s_pixels = heap_caps_malloc(480 * 480 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_requests = xQueueCreate(4, sizeof(air_request_t));
    if (!s_pixels || !s_requests) goto fail;
    air_core_init(&s_core);
    restore();
    if (xTaskCreate(air_task, "outdoor_air", 8192, NULL, 2, NULL) != pdPASS) goto fail;
    return true;
fail:
    free(s_pixels);
    s_pixels = NULL;
    if (s_requests) vQueueDelete(s_requests);
    s_requests = NULL;
    ESP_LOGE(TAG, "Outdoor Air allocation failed");
    return false;
}

cJSON *air_guardian_command(const char *command, const cJSON *params, const char *request_id,
                            noise_ctrl_session_generation_t generation) {
    air_request_t request = {.generation = generation};
    const char *error = NULL;
    if (!command || strlen(command) >= sizeof(request.command) || !request_id
        || strlen(request_id) >= sizeof(request.request_id)) error = "Invalid request metadata";
    else if (!s_requests) error = "Outdoor Air is unavailable";
    else if (params && !(request.params = cJSON_Duplicate(params, true))) error = "Outdoor Air allocation failed";
    else {
        strcpy(request.command, command);
        strcpy(request.request_id, request_id);
        if (xQueueSend(s_requests, &request, 0) != pdTRUE) error = "Outdoor Air is busy; retry shortly";
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
