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
#include "noise_control.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
bool air_guardian_init(void);
cJSON *air_guardian_command(const char *command, const cJSON *params, const char *request_id,
                            noise_ctrl_session_generation_t generation);
// Presents a native RGB565 dashboard during connected/reconnecting states while images are inactive.
// 在已连接或重连期间且未展示图片时呈现原生 RGB565 仪表盘。
bool led_status_air_draw(const uint16_t *pixels);
// Queues the next persistent view on the application worker.
// 将下一个需要持久保存的视图交给应用后台任务。
void air_guardian_cycle_view(void);
#ifdef __cplusplus
}
#endif
