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

#define KID_ITEMS 10
#define KID_STAR_GOAL 20
#define KID_REFRESH_US 60000000LL

typedef struct {
    char id[32], time[6], label[48], icon[32], who[32];
    int minutes;
    bool done;
    int64_t done_at;
} kid_item_t;

typedef struct {
    char child[32], date[11];
    kid_item_t items[KID_ITEMS];
    unsigned item_count;
    int count, goal;
    char reward_label[64], reward_icon[32], note[96];
    bool active;
} kid_model_t;

typedef struct {
    bool (*event)(const char *summary, const cJSON *data);
    void (*chime)(bool gentle);
} kid_hooks_t;

typedef struct {
    bool dirty, started;
    int64_t last_us;
} kid_refresh_t;

void kid_model_init(kid_model_t *model);
bool kid_model_clock_valid(int64_t now);
void kid_model_date(int64_t now, int offset, char out[11]);
// Applies a command to a caller-owned candidate; changed describes stored content.
// 将命令应用于调用方持有的候选模型；changed 表示存储内容是否变化。
bool kid_model_command(kid_model_t *model, const char *command, const cJSON *params,
                        int64_t now, int offset, bool *changed, const char **error);
// Returns the earliest unfinished item for the active local date, or -1.
// 返回当前本地日期中最早未完成的条目；无可选条目时返回 -1。
int kid_model_current(const kid_model_t *model, int64_t now, int offset);
bool kid_model_rollover(kid_model_t *model, int64_t now, int offset);
bool kid_model_green(kid_model_t *model, int64_t now, int offset, const kid_hooks_t *hooks);
cJSON *kid_model_json(const kid_model_t *model, int64_t now, int offset);
void kid_model_render(epaper_cards_canvas_t *canvas, const kid_model_t *model, int64_t now, int offset);
void kid_refresh_request(kid_refresh_t *refresh);
int64_t kid_refresh_wait(const kid_refresh_t *refresh, int64_t uptime_us);
void kid_refresh_started(kid_refresh_t *refresh, int64_t uptime_us);
