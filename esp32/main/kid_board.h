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
#include "kid_model.h"
#include "noise_control.h"
#include "reterminal_io.h"

bool kid_board_init(bool (*keys_ready)(void));
void kid_board_notify(void);
bool kid_board_key(reterminal_key_t key);
cJSON *kid_board_command(const char *command, const cJSON *params,
                          const char *request_id, noise_ctrl_session_generation_t generation);
