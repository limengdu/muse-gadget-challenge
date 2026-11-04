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

#ifdef __cplusplus
extern "C" {
#endif

#define DOOR_SIGN_TITLE_BYTES 41
#define DOOR_SIGN_SUBTITLE_BYTES 81
#define DOOR_SIGN_OWNER_BYTES 25
#define DOOR_SIGN_REPLY_BYTES 29
#define DOOR_SIGN_REPLIES 3

typedef enum {
    DOOR_SIGN_MEETING, DOOR_SIGN_FOCUS, DOOR_SIGN_FREE,
    DOOR_SIGN_AWAY, DOOR_SIGN_CUSTOM, DOOR_SIGN_STATE_COUNT
} door_sign_state_t;

typedef struct {
    bool active;
    door_sign_state_t state;
    uint32_t color;
    char title[DOOR_SIGN_TITLE_BYTES];
    char subtitle[DOOR_SIGN_SUBTITLE_BYTES];
    char owner[DOOR_SIGN_OWNER_BYTES];
    unsigned reply_count;
    char replies[DOOR_SIGN_REPLIES][DOOR_SIGN_REPLY_BYTES];
    int64_t until;
} door_sign_model_t;

typedef struct {
    door_sign_model_t sign;
    uint32_t revision;
    int64_t epoch;
    int local_second;
    bool clock_valid;
    int64_t sent_us;
    int sent_reply;
} door_sign_snapshot_t;

// Copy the current sign and submit a reply without waiting for device services.
// 复制当前门牌并提交回复，调用时不等待设备服务。
bool door_sign_snapshot(door_sign_snapshot_t *out);
bool door_sign_reply(unsigned index, uint32_t revision);

#ifdef __cplusplus
}
#endif
