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

#include "card_mask.h"
#include <stddef.h>

bool card_mask_begin(card_mask_decoder_t *d, const uint8_t *data,
                     uint16_t encoded_bytes, unsigned width, unsigned height) {
    if (!d) return false;
    *d = (card_mask_decoder_t){0};
    if (!data || !width || width > CARD_MASK_ROW_BYTES * 8 || !height || height > 480) return false;
    unsigned bytes = encoded_bytes & ~CARD_MASK_RAW;
    unsigned stride = (width + 7) / 8;
    bool raw = (encoded_bytes & CARD_MASK_RAW) != 0;
    if (!bytes || (raw && bytes != stride * height)) return false;
    *d = (card_mask_decoder_t){.cursor = data, .end = data + bytes, .stride = stride,
        .rows = height, .remaining = stride * height, .raw = raw};
    return true;
}

static const uint8_t *invalid(card_mask_decoder_t *d) { d->rows = 0; return NULL; }

// Reconstructs each row by XORing decoded differences with the previous row.
// 将解码后的差分与上一行异或，恢复当前行。
const uint8_t *card_mask_next(card_mask_decoder_t *d) {
    if (!d || !d->rows) return NULL;
    for (unsigned i = 0; i < d->stride; i++) {
        if (d->raw) {
            if (d->cursor == d->end) return invalid(d);
            d->row[i] = *d->cursor++;
        } else {
            if (!d->run) {
                if (d->cursor == d->end) return invalid(d);
                uint8_t token = *d->cursor++;
                d->run = (token & 127) + 1;
                d->zero = (token & 128) != 0;
                if (d->run > d->remaining) return invalid(d);
            }
            uint8_t value = 0;
            if (!d->zero) {
                if (d->cursor == d->end) return invalid(d);
                value = *d->cursor++;
            }
            d->row[i] ^= value;
            d->run--;
        }
        d->remaining--;
    }
    if (!--d->rows && (d->cursor != d->end || d->run)) return invalid(d);
    return d->row;
}
