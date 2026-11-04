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

#define AIR_ADVICE_BYTES 81
#define AIR_PLACE_BYTES 41
#define AIR_MIN_EPOCH 1704067200LL
#define AIR_MAX_EPOCH 4102444799LL
#define AIR_STALE_S (3 * 60 * 60)

typedef enum { AIR_AMBIENT, AIR_NUMBERS, AIR_NIGHT, AIR_VIEW_COUNT } air_view_t;
typedef enum {
    AIR_GOOD, AIR_MODERATE, AIR_SENSITIVE, AIR_UNHEALTHY,
    AIR_VERY_UNHEALTHY, AIR_HAZARDOUS, AIR_CATEGORY_COUNT
} air_category_t;
typedef struct {
    bool present;
    int aqi;
    double pm25;
    air_category_t category;
    char advice[AIR_ADVICE_BYTES], place[AIR_PLACE_BYTES];
    int64_t updated_utc;
} air_snapshot_t;
typedef struct {
    air_snapshot_t snapshot;
    air_view_t view;
} air_core_t;
typedef bool (*air_save_fn)(const air_core_t *state, void *ctx);

extern const char *const air_view_names[AIR_VIEW_COUNT];
extern const char *const air_category_names[AIR_CATEGORY_COUNT];
extern const char *const air_category_labels[AIR_CATEGORY_COUNT];
void air_core_init(air_core_t *c);
bool air_snapshot_parse(air_snapshot_t *out, const cJSON *params);
int64_t air_snapshot_age(const air_core_t *c, int64_t epoch);
cJSON *air_core_json(const air_core_t *c);
bool air_core_restore(air_core_t *c, const cJSON *record);
cJSON *air_core_command(air_core_t *c, const char *command, const cJSON *params,
                        int64_t epoch, air_save_fn save, void *ctx);
void air_guardian_render(card_rgb565_t *canvas, const air_core_t *c,
                         int64_t epoch, int local_minutes);
