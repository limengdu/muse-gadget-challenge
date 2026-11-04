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
#include "cJSON.h"
#include "card_rgb565.h"
#include <stdbool.h>
#include <stdint.h>

#define AIR_METRICS 4
#define AIR_LIMITS 6
#define AIR_MINUTES (7 * 24 * 60)
#define AIR_POINTS 24
#define AIR_COOLDOWN_S (30 * 60)
#define AIR_ROOM_BYTES 41

typedef enum { AIR_AMBIENT, AIR_NUMBERS, AIR_NIGHT, AIR_AUTO } air_view_t;
typedef struct {
    float limits[AIR_LIMITS];
    char room[AIR_ROOM_BYTES];
    air_view_t view;
} air_settings_t;
typedef struct {
    int64_t time_s;
    float values[AIR_METRICS];
    uint8_t valid;
} air_sample_t;
typedef struct {
    bool active, announced, sent_before;
    int64_t last_sent_s, last_event_time;
    bool last_event_epoch;
    char last_message[192];
} air_alarm_t;
typedef struct {
    air_settings_t settings;
    air_sample_t *history;
    unsigned head, count;
    bool sampled;
    int64_t sampled_s;
    float current[AIR_METRICS];
    uint8_t valid;
    air_alarm_t alarms[AIR_LIMITS];
} air_core_t;
typedef struct { float min, avg, max; unsigned count; } air_bucket_t;
typedef struct {
    unsigned count, step_min;
    int64_t times[AIR_POINTS];
    air_bucket_t metrics[AIR_METRICS][AIR_POINTS];
} air_buckets_t;
typedef bool (*air_post_fn)(const char *summary, const cJSON *data, void *ctx);
typedef bool (*air_save_fn)(const air_settings_t *settings, void *ctx);

extern const char *const air_metric_names[AIR_METRICS];
extern const char *const air_limit_names[AIR_LIMITS];
extern const char *const air_view_names[4];
void air_core_init(air_core_t *c, air_sample_t *storage);
void air_core_observe(air_core_t *c, const float values[AIR_METRICS], uint8_t valid, int64_t now_s);
void air_core_evaluate(air_core_t *c, int64_t now_s, int64_t epoch, air_post_fn post, void *ctx);
void air_core_buckets(const air_core_t *c, int hours, int step_min, int64_t now_s, air_buckets_t *out);
air_view_t air_effective_view(air_view_t view, int minutes);
int air_core_level(const air_core_t *c, int metric);
const char *air_core_mood(const air_core_t *c);
const char *air_core_advice(const air_core_t *c);
bool air_settings_parse(air_settings_t *out, const air_settings_t *base, const cJSON *params, bool view);
cJSON *air_settings_json(const air_settings_t *s);
cJSON *air_core_command(air_core_t *c, const char *command, const cJSON *params,
                        int64_t now_s, int64_t epoch, int minutes, air_save_fn save, void *ctx);
void air_guardian_render(card_rgb565_t *canvas, const air_core_t *c, int64_t now_s,
                         int minutes, bool pulse);
