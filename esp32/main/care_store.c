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

#include "care_store.h"
#include "care_memory.h"
#include <stdlib.h>
#include <string.h>
#include "nvs.h"

#define CARE_STORE_MAGIC 0x43415245u
#define CARE_STORE_VERSION 1u

typedef struct {
    uint32_t magic, version, checksum;
    care_model_t model;
} care_record_t;

static uint32_t checksum(const care_model_t *m) {
    const uint8_t *bytes = (const uint8_t *)m;
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < sizeof(*m); i++) value = (value ^ bytes[i]) * 16777619u;
    return value;
}

// Stores one versioned, checksummed NVS blob, including runtime reminder status.
// 将含提醒运行状态的数据保存为带版本和校验值的单个 NVS 数据块。
bool care_store_save(const care_model_t *model) {
    care_record_t *record = care_memory_alloc(sizeof(*record));
    if (!record) return false;
    record->magic = CARE_STORE_MAGIC; record->version = CARE_STORE_VERSION;
    memcpy(&record->model, model, sizeof(*model)); record->checksum = checksum(&record->model);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("care_board", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "model", record, sizeof(*record));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    free(record); return err == ESP_OK;
}

bool care_store_load(care_model_t *model) {
    care_record_t *record = care_memory_alloc(sizeof(*record));
    if (!record) return false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("care_board", NVS_READONLY, &handle);
    size_t size = sizeof(*record);
    if (err == ESP_OK) { err = nvs_get_blob(handle, "model", record, &size); nvs_close(handle); }
    bool ok = err == ESP_OK && size == sizeof(*record) && record->magic == CARE_STORE_MAGIC
        && record->version == CARE_STORE_VERSION && record->checksum == checksum(&record->model)
        && record->model.reminder_count <= CARE_REMINDERS && record->model.message_count <= CARE_MESSAGES;
    if (ok) {
        memcpy(model, &record->model, sizeof(*model));
        memset(model->key_seen, 0, sizeof(model->key_seen));
        memset(model->last_key_us, 0, sizeof(model->last_key_us));
    }
    free(record); return ok;
}
