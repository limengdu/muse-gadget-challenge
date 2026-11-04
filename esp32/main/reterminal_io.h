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
#include "driver/i2c_master.h"

typedef enum { RETERMINAL_GREEN, RETERMINAL_LEFT, RETERMINAL_RIGHT } reterminal_key_t;
typedef bool (*reterminal_ready_cb)(void);
typedef void (*reterminal_key_cb)(reterminal_key_t key);

bool reterminal_io_init(reterminal_ready_cb ready, reterminal_key_cb key);
bool reterminal_io_i2c_bus(i2c_master_bus_handle_t *out);
void reterminal_io_due(bool due);
void reterminal_io_chime(bool gentle);
void reterminal_io_silence(void);
bool reterminal_io_rtc_read(int64_t *epoch);
bool reterminal_io_rtc_write(int64_t epoch);
// Converts the PCF8563 UTC register block, validating BCD, date and integrity.
// 转换 PCF8563 的 UTC 寄存器块，校验 BCD、日期和完整性标记。
bool reterminal_io_rtc_decode(const uint8_t raw[9], int64_t *epoch);
bool reterminal_io_rtc_encode(int64_t epoch, uint8_t raw[8]);
