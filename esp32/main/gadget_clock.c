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

#include "gadget_clock.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "cJSON.h"
#include "config_store.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CLOCK_MIN_EPOCH 1704067200LL
#define CLOCK_MAX_EPOCH 4102444799LL
#define OFFSET_KEY "utc_offset_min"

static atomic_int s_offset;
static atomic_flag s_setting = ATOMIC_FLAG_INIT;
static bool s_sntp_started;
static const char *TAG = "link.gadget_clock";

void gadget_clock_init(void) {
    char value[16];
    if (config_get_str(OFFSET_KEY, value, sizeof(value))) {
        char *end;
        long offset = strtol(value, &end, 10);
        if (end != value && !*end && offset >= -840 && offset <= 840) {
            atomic_store(&s_offset, (int)offset);
        }
    }
}

// Starts the SNTP client after the station receives an IP address.
// 在无线网络获得 IP 地址后启动 SNTP 客户端。
void gadget_clock_wifi_up(void) {
    if (s_sntp_started) return;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.wait_for_sync = false;
    esp_err_t err = esp_netif_sntp_init(&config);
    s_sntp_started = err == ESP_OK;
    ESP_LOGI(TAG, "SNTP init status=%d", (int)err);
}

// Serializes offset persistence and the system UTC update.
// 串行完成时差保存与系统 UTC 更新。
bool gadget_clock_set(int64_t utc_epoch, int utc_offset_min) {
    if (utc_epoch < CLOCK_MIN_EPOCH || utc_epoch > CLOCK_MAX_EPOCH
        || utc_offset_min < -840 || utc_offset_min > 840) return false;
    if (atomic_flag_test_and_set(&s_setting)) return false;
    char value[16];
    snprintf(value, sizeof(value), "%d", utc_offset_min);
    bool ok = config_set_str(OFFSET_KEY, value);
    struct timeval tv = {.tv_sec = (time_t)utc_epoch};
    if (ok) ok = settimeofday(&tv, NULL) == 0;
    if (ok) atomic_store(&s_offset, utc_offset_min);
    atomic_flag_clear(&s_setting);
    return ok;
}

int64_t gadget_clock_epoch(void) {
    return (int64_t)time(NULL);
}

bool gadget_clock_valid(void) {
    int64_t epoch = gadget_clock_epoch();
    return epoch >= CLOCK_MIN_EPOCH && epoch <= CLOCK_MAX_EPOCH;
}

bool gadget_clock_local(struct tm *out) {
    if (!out || !gadget_clock_valid()) return false;
    time_t local = (time_t)(gadget_clock_epoch() + atomic_load(&s_offset) * 60LL);
    return gmtime_r(&local, out) != NULL;
}

int gadget_clock_minutes_of_day(void) {
    struct tm local;
    return gadget_clock_local(&local) ? local.tm_hour * 60 + local.tm_min : -1;
}

typedef struct {
    int64_t epoch;
    int offset;
    noise_ctrl_session_generation_t generation;
    char request_id[];
} set_args_t;

// Applies the clock and NVS update on a worker task, then answers Muse.
// 在后台任务中更新时钟及 NVS，然后回复 Muse。
static void set_task(void *arg) {
    set_args_t *args = arg;
    bool ok = gadget_clock_set(args->epoch, args->offset);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", ok);
    if (!ok) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "code", "storage_failed");
        cJSON_AddStringToObject(error, "message", "clock update failed");
        cJSON_AddItemToObject(result, "error", error);
    } else {
        cJSON *payload = cJSON_CreateObject();
        cJSON_AddNumberToObject(payload, "utc_epoch", (double)args->epoch);
        cJSON_AddNumberToObject(payload, "utc_offset_min", args->offset);
        cJSON_AddItemToObject(result, "payload", payload);
    }
    noise_ctrl_send_command_result(args->generation, args->request_id, result);
    free(args);
    vTaskDelete(NULL);
}

cJSON *gadget_clock_command(const cJSON *params, const char *request_id,
                            noise_ctrl_session_generation_t generation) {
    cJSON *epoch = cJSON_GetObjectItemCaseSensitive(params, "utc_epoch");
    cJSON *offset = cJSON_GetObjectItemCaseSensitive(params, "utc_offset_min");
    const char *error = NULL;
    if (!cJSON_IsNumber(epoch) || !cJSON_IsNumber(offset)
        || !isfinite(epoch->valuedouble) || !isfinite(offset->valuedouble)
        || epoch->valuedouble < CLOCK_MIN_EPOCH || epoch->valuedouble > CLOCK_MAX_EPOCH
        || offset->valuedouble < -840 || offset->valuedouble > 840
        || floor(epoch->valuedouble) != epoch->valuedouble
        || floor(offset->valuedouble) != offset->valuedouble || !request_id) {
        error = "utc_epoch (2024-2099) and utc_offset_min (-840..840) must be integers";
    }
    set_args_t *args = error ? NULL : malloc(sizeof(*args) + strlen(request_id) + 1);
    if (!error && !args) error = "clock allocation failed";
    cJSON *result = cJSON_CreateObject();
    if (args) {
        args->epoch = (int64_t)epoch->valuedouble;
        args->offset = (int)offset->valuedouble;
        args->generation = generation;
        strcpy(args->request_id, request_id);
        if (xTaskCreate(set_task, "clock_set", 3072, args, 3, NULL) == pdPASS) {
            cJSON_AddBoolToObject(result, "_async", true);
            return result;
        }
        free(args);
        error = "clock task start failed";
    }
    cJSON_AddBoolToObject(result, "ok", false);
    cJSON *details = cJSON_CreateObject();
    cJSON_AddStringToObject(details, "code", "invalid_params");
    cJSON_AddStringToObject(details, "message", error);
    cJSON_AddItemToObject(result, "error", details);
    return result;
}
