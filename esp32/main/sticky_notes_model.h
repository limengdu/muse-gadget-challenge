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

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#ifdef __cplusplus
extern "C" {
#endif
#define STICKY_NOTES_LISTS 4
#define STICKY_NOTES_ITEMS 20
#define STICKY_NOTES_TEXT 128
#define STICKY_NOTES_WHO 32
#define STICKY_NOTES_NAME 24
#define STICKY_NOTES_TITLE 32
#define STICKY_NOTES_PATH "gadget/fridge-notes/lists.json"
#define STICKY_NOTES_CHUNK 768

typedef struct {
    uint32_t id;
    bool done;
    int64_t added_at;
    char text[STICKY_NOTES_TEXT], who[STICKY_NOTES_WHO];
} sticky_notes_item_t;
typedef struct {
    char name[STICKY_NOTES_NAME], title[STICKY_NOTES_TITLE];
    unsigned count;
    sticky_notes_item_t items[STICKY_NOTES_ITEMS];
} sticky_notes_list_t;
typedef struct {
    uint32_t version, revision, next_id;
    unsigned count;
    sticky_notes_list_t lists[STICKY_NOTES_LISTS];
} sticky_notes_model_t;
sticky_notes_model_t *sticky_notes_model_alloc(void);
void sticky_notes_model_init(sticky_notes_model_t *model);
bool sticky_notes_handles(const char *command);
// Applies a validated command atomically; changed is false on error or a read.
// 原子应用已校验的命令；出错或读取时 changed 为 false。
cJSON *sticky_notes_apply(sticky_notes_model_t *model, const char *command,
                          const cJSON *params, int64_t now, bool *changed);
cJSON *sticky_notes_json(const sticky_notes_model_t *model, const char *list);
bool sticky_notes_restore(sticky_notes_model_t *model, const cJSON *json);
// Encodes one file-write request and advances offset only after allocation succeeds.
// 编码一次文件写入请求，仅在分配成功后推进偏移量。
char *sticky_notes_write_body(const char *json, size_t *offset);
void sticky_notes_register(cJSON *commands);
#ifdef __cplusplus
}
#endif
