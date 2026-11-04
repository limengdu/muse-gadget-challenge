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
#include "epaper_cards.h"

#define CARE_REMINDERS 8
#define CARE_MESSAGES 3
#define CARE_TEXT 192

typedef enum { CARE_PENDING, CARE_DUE, CARE_DONE, CARE_MISSED } care_status_t;
typedef enum { CARE_GREEN, CARE_LEFT, CARE_RIGHT, CARE_KEYS } care_key_t;

typedef struct {
    char from[40], text[CARE_TEXT];
    int64_t time;
    bool read;
} care_message_t;

typedef struct {
    char id[32], time[6], label[64], icon[32];
    int minutes, escalate_min;
    bool daily;
    care_status_t status;
    int64_t next_due, due_at, done_at, next_chime;
    unsigned chimes;
} care_reminder_t;

typedef struct {
    char person[32], family[40], title[56];
    char weather_text[88], weather_icon[32];
    double weather_temp;
    bool weather_set, active;
    care_message_t messages[CARE_MESSAGES];
    care_reminder_t reminders[CARE_REMINDERS];
    unsigned message_count, reminder_count;
    int schedule_offset;
    int64_t day, updated_at, last_keys[CARE_KEYS], last_checkin;
    int64_t last_key_us[CARE_KEYS];
    bool key_seen[CARE_KEYS], key_time_epoch[CARE_KEYS], footer_check;
    char footer[128];
} care_model_t;

typedef struct {
    bool (*event)(const char *summary, const cJSON *data);
    void (*chime)(bool gentle);
} care_hooks_t;

void care_model_init(care_model_t *model);
// Applies a validated patch to a caller-owned candidate model.
// 将通过验证的补丁应用到调用方持有的候选模型。
bool care_model_patch(care_model_t *model, const cJSON *patch, int64_t now,
                       int offset, bool *new_message, const char **error);
// Advances reminders and returns the next UTC deadline, or zero without a clock.
// 推进提醒状态并返回下次 UTC 截止时间；时钟无效时返回零。
int64_t care_model_step(care_model_t *model, int64_t now, int offset,
                        const care_hooks_t *hooks, bool *changed);
bool care_model_key(care_model_t *model, care_key_t key, int64_t now, int offset,
                     int64_t uptime_us, const care_hooks_t *hooks);
cJSON *care_model_json(const care_model_t *model);
void care_model_render(epaper_cards_canvas_t *canvas, const care_model_t *model,
                        int64_t now, int offset, bool sensor_valid, double temp, double humidity);
int care_model_oldest(const care_model_t *model);
void care_model_time(int64_t epoch, int offset, char out[6]);
