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
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GADGET_EVENTS_PENDING 16
#define GADGET_EVENTS_HISTORY 32
#define GADGET_EVENTS_REPLY_BYTES 601

// Initialize once during app startup, before producers start.
// 在应用启动期间、事件生产者启动之前初始化一次。
bool gadget_events_init(void);
// Copies source, summary and data into a bounded RAM queue without waiting.
// 将来源、摘要和数据复制到有界内存队列，调用时不等待。
bool gadget_events_post(const char *source, const char *summary, const cJSON *data);
// Returns an owned command result; since is a nonnegative integer sequence.
// 返回由调用方持有的命令结果；since 为非负整数序号。
cJSON *gadget_events_poll(uint64_t since);
cJSON *gadget_events_command(const cJSON *params);
// Copies the last event reply, preserving complete UTF-8 characters.
// 复制最近一次事件的回复，保留完整的 UTF-8 字符。
void gadget_events_reply(char *out, size_t capacity);
bool gadget_events_console_command(const char *line);

#ifdef __cplusplus
}
#endif
