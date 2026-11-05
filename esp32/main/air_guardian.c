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
#include "stack_monitor.h"
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#define AIR_STATE_KEY "outdoor_air"
#define AIR_REQUEST_BYTES 96
#define AIR_TASK_STACK 8192
// TODO(D1 NVS high-water measurement): Tune the save stack with erase-path margin.
#define AIR_SAVE_STACK 4096

typedef struct {
    char command[24], request_id[AIR_REQUEST_BYTES];
    cJSON *params;
    noise_ctrl_session_generation_t generation;
    bool cycle, wake;
} air_request_t;

static air_core_t s_core;
static QueueHandle_t s_requests;
static uint16_t *s_pixels;
static atomic_bool s_display_dirty;
static const char *TAG = "link.air";

static int64_t epoch(void) { return gadget_clock_valid() ? gadget_clock_epoch() : 0; }

static void clock_changed(void) {
    air_request_t request = {.wake = true};
    if (s_requests) (void)xQueueSend(s_requests, &request, 0);
}

void air_guardian_display_changed(void) {
    atomic_store(&s_display_dirty, true);
    clock_changed();
}

#if CONFIG_SPIRAM
typedef struct {
    const char *json;
    TaskHandle_t waiter;
    bool ok;
} air_save_t;

// Writes the serialized snapshot on a temporary internal task stack.
// 在临时内部任务栈上写入序列化快照。
static void save_task(void *arg) {
    air_save_t *job = arg;
    job->ok = config_set_str(AIR_STATE_KEY, job->json);
    stack_monitor_record(NULL);
    xTaskNotifyGive(job->waiter);
    vTaskDelete(NULL);
}
#endif

static bool save(const air_core_t *state, void *ctx) {
    (void)ctx;
    cJSON *p = air_core_json(state);
    char *json = p ? cJSON_PrintUnformatted(p) : NULL;
    bool ok = false;
    if (json) {
#if CONFIG_SPIRAM
        air_save_t job = {.json = json, .waiter = xTaskGetCurrentTaskHandle()};
        if (xTaskCreate(save_task, "air_save", AIR_SAVE_STACK, &job, 3, NULL) == pdPASS) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            ok = job.ok;
        }
#else
        ok = config_set_str(AIR_STATE_KEY, json);
#endif
    }
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

static TickType_t next_wait(int64_t current) {
    int64_t seconds = air_guardian_next_change(&s_core, current);
    if (seconds < 0) return portMAX_DELAY;
    uint64_t ticks = (uint64_t)seconds * configTICK_RATE_HZ;
    return ticks >= (uint64_t)(TickType_t)portMAX_DELAY ? portMAX_DELAY - 1 : (TickType_t)ticks;
}

// Applies updates after persistence completes, then renders the native LCD.
// 持久化完成后应用快照更新，并绘制原生 LCD 画面。
static void air_task(void *arg) {
    (void)arg;
    card_rgb565_t canvas;
    card_rgb565_canvas(&canvas, s_pixels, 480 * 480 * 2, 480, 480);
    air_screen_t shown;
    bool shown_valid = false;
    bool stack_logged = false;
    const char *reason = "initial";
    for (;;) {
        if (atomic_exchange(&s_display_dirty, false)) {
            shown_valid = false;
            reason = "display";
        }
        int64_t current = epoch();
        int local_minutes = current ? (int)((current / 60 + gadget_clock_offset_minutes() + 1440) % 1440) : -1;
        air_screen_t next;
        air_guardian_screen(&next, &s_core, current, local_minutes);
        bool active = led_status_air_active();
        if (active && (!shown_valid || memcmp(&next, &shown, sizeof(next)))) {
            if (shown_valid) {
                if (next.view != shown.view) reason = "view";
                else if (!strcmp(reason, "snapshot")) reason = "snapshot";
                else if (next.age_label != shown.age_label) reason = "age";
                else reason = "clock";
            }
            air_guardian_render_screen(&canvas, &next);
            if (led_status_air_draw(s_pixels)) {
                memcpy(&shown, &next, sizeof(shown));
                shown_valid = true;
                ESP_LOGI(TAG, "redraw reason=%s", reason);
            }
        }
        if (!stack_logged) {
            stack_monitor_record(NULL);
            stack_logged = true;
        }
        reason = "clock";
        air_request_t request;
        bool received = xQueueReceive(s_requests, &request, active ? next_wait(current) : portMAX_DELAY) == pdTRUE;
        if (received && !request.wake) {
            if (request.cycle) {
                reason = "view";
                cJSON *params = cJSON_CreateObject();
                cJSON_AddStringToObject(params, "view", air_view_names[(s_core.view + 1) % AIR_VIEW_COUNT]);
                cJSON *r = air_core_command(&s_core, "air.set_view", params, epoch(), save, NULL);
                cJSON *ok = cJSON_GetObjectItemCaseSensitive(r, "ok");
                if (!cJSON_IsTrue(ok)) ESP_LOGW(TAG, "view could not be saved");
                cJSON_Delete(r);
                cJSON_Delete(params);
            } else {
                if (!strcmp(request.command, "air.update")) reason = "snapshot";
                cJSON *r = air_core_command(&s_core, request.command, request.params, epoch(), save, NULL);
                noise_ctrl_send_command_result(request.generation, request.request_id, r);
            }
            cJSON_Delete(request.params);
        }
    }
}

bool air_guardian_init(void) {
    s_pixels = heap_caps_malloc(480 * 480 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_requests = xQueueCreate(4, sizeof(air_request_t));
    if (!s_pixels || !s_requests) goto fail;
    air_core_init(&s_core);
    restore();
#if CONFIG_SPIRAM
    if (xTaskCreateWithCaps(air_task, "outdoor_air", AIR_TASK_STACK, NULL, 2, NULL,
                            MALLOC_CAP_SPIRAM) != pdPASS) goto fail;
#else
    if (xTaskCreate(air_task, "outdoor_air", AIR_TASK_STACK, NULL, 2, NULL) != pdPASS) goto fail;
#endif
    gadget_clock_set_change_cb(clock_changed);
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
