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
#include <time.h>
#include "noise_control.h"

#ifdef __cplusplus
extern "C" {
#endif

void gadget_clock_init(void);
void gadget_clock_wifi_up(void);
bool gadget_clock_set(int64_t utc_epoch, int utc_offset_min);
bool gadget_clock_valid(void);
bool gadget_clock_local(struct tm *out);
int64_t gadget_clock_epoch(void);
// Returns local minutes since midnight, or -1 before time is valid.
// 返回本地午夜起的分钟数，时间有效前返回 -1。
int gadget_clock_minutes_of_day(void);
cJSON *gadget_clock_command(const cJSON *params, const char *request_id,
                            noise_ctrl_session_generation_t generation);

#ifdef __cplusplus
}
#endif
