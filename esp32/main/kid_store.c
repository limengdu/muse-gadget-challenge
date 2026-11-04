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

#include "kid_store.h"
#include "kid_memory.h"
#include <stdlib.h>
#include <string.h>
#include "nvs.h"

#define KID_STORE_MAGIC 0x4b494431u
#define KID_STORE_VERSION 1u

typedef struct {
    uint32_t magic, version, checksum;
    kid_model_t model;
} kid_record_t;

static uint32_t checksum(const kid_model_t *m) {
    const uint8_t *bytes = (const uint8_t *)m;
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < sizeof(*m); i++) value = (value ^ bytes[i]) * 16777619u;
    return value;
}

// Stores one versioned, checksummed NVS blob, including step confirmations and stars.
// 将含步骤完成情况与星星的数据保存为带版本和校验值的单个 NVS 数据块。
bool kid_store_save(const kid_model_t *model) {
    kid_record_t *record = kid_memory_alloc(sizeof(*record));
    if (!record) return false;
    record->magic = KID_STORE_MAGIC; record->version = KID_STORE_VERSION;
    memcpy(&record->model, model, sizeof(*model)); record->checksum = checksum(&record->model);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("kid_board", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "model", record, sizeof(*record));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    free(record); return err == ESP_OK;
}

bool kid_store_load(kid_model_t *model) {
    kid_record_t *record = kid_memory_alloc(sizeof(*record));
    if (!record) return false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("kid_board", NVS_READONLY, &handle);
    size_t size = sizeof(*record);
    if (err == ESP_OK) { err = nvs_get_blob(handle, "model", record, &size); nvs_close(handle); }
    bool ok = err == ESP_OK && size == sizeof(*record) && record->magic == KID_STORE_MAGIC
        && record->version == KID_STORE_VERSION && record->checksum == checksum(&record->model)
        && record->model.item_count <= KID_ITEMS && record->model.goal >= 1
        && record->model.goal <= KID_STAR_GOAL && record->model.count >= 0 && record->model.count <= 999;
    if (ok) {
        memcpy(model, &record->model, sizeof(*model));
    }
    free(record); return ok;
}
