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

#define CARD_MASK_RAW 0x8000u
#define CARD_MASK_ROW_BYTES 16

typedef struct {
    const uint8_t *cursor, *end;
    unsigned stride, rows, remaining, run;
    bool raw, zero;
    uint8_t row[CARD_MASK_ROW_BYTES];
} card_mask_decoder_t;

// Decodes a mask one row at a time using a 16-byte row buffer.
// 使用 16 字节行缓冲区逐行解码图案。
bool card_mask_begin(card_mask_decoder_t *decoder, const uint8_t *data,
                     uint16_t encoded_bytes, unsigned width, unsigned height);
const uint8_t *card_mask_next(card_mask_decoder_t *decoder);
