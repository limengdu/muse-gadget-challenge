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

#include "muse_door_sign.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DOOR_SIGN_STORAGE_BYTES 2048

typedef struct {
    int64_t epoch;
    int local_second;
    bool valid;
} door_sign_time_t;

void door_sign_defaults(door_sign_model_t *out, door_sign_state_t state);
const char *door_sign_state_name(door_sign_state_t state);
// Parse a complete replacement; HH:MM means the next occurrence in local time.
// 解析完整替换内容；HH:MM 表示本地时间中下一次到达的时刻。
bool door_sign_parse(const cJSON *params, door_sign_time_t now,
                     door_sign_model_t *out, const char **error);
cJSON *door_sign_json(const door_sign_model_t *sign);
char *door_sign_encode(const door_sign_model_t *sign);
bool door_sign_decode(const char *stored, door_sign_model_t *out);
bool door_sign_expire(door_sign_model_t *sign, door_sign_time_t now);
bool door_sign_tap(const door_sign_model_t *sign, unsigned index, int64_t now_us,
                   int64_t *last_us, door_sign_time_t now, char summary[256]);

#ifdef __cplusplus
}
#endif
