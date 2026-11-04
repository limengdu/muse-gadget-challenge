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

typedef struct {
    bool active, waiting, alert;
    uint32_t card_id;
    int64_t next_ms;
    char name[49], heading[80], text[481], time[24];
} muse_home_eyes_view_t;

void muse_home_eyes_publish(const muse_home_eyes_view_t *view);
void muse_home_eyes_bind(void (*notify)(void));
void muse_home_eyes_ready(void);
bool muse_home_eyes_is_ready(void);
uint32_t muse_home_eyes_take_ack(void);
bool muse_home_eyes_dismiss(void);
void muse_home_eyes_tick(void);
