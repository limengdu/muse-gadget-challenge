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
#include "sticky_notes_model.h"
#include "noise_control.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    unsigned count, selected;
    char names[STICKY_NOTES_LISTS][STICKY_NOTES_NAME];
    char titles[STICKY_NOTES_LISTS][STICKY_NOTES_TITLE];
    sticky_notes_list_t list;
    uint32_t revision;
    bool pending, save_failed;
    int synced_minute;
} sticky_notes_view_t;
bool sticky_notes_init(void);
void sticky_notes_watch(void (*redraw)(void));
bool sticky_notes_view(unsigned selected, sticky_notes_view_t *out);
cJSON *sticky_notes_command(const char *command, const cJSON *params,
                            const char *request_id, noise_ctrl_session_generation_t generation);
// Queues a local checkbox or clear operation; false reports a busy queue.
// 将本地勾选或清理操作入队；返回 false 表示队列繁忙。
bool sticky_notes_touch(const char *list, uint32_t id, bool clear);
#ifdef __cplusplus
}
#endif
