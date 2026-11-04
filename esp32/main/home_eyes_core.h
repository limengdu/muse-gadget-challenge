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
#include "cJSON.h"

#define HOME_EYES_SIDE 32
#define HOME_EYES_PIXELS (HOME_EYES_SIDE * HOME_EYES_SIDE)
#define HOME_EYES_NAME 49
#define HOME_EYES_QUESTION 193
#define HOME_EYES_TEXT 481
#define HOME_EYES_CHANGE 1u
#define HOME_EYES_ALERT_ACK 2u
#define HOME_EYES_MESSAGE_ACK 4u

typedef struct {
    bool active, waiting, reference_valid, candidate_valid;
    bool message, verdict, alert, reminded;
    unsigned interval_min, sensitivity, events;
    uint32_t card_seq, message_id, verdict_id;
    int64_t next_ms, until, message_ms, alarm_ms;
    int64_t message_epoch;
    char name[HOME_EYES_NAME], question[HOME_EYES_QUESTION];
    char text[HOME_EYES_TEXT], from[HOME_EYES_NAME], verdict_text[HOME_EYES_TEXT];
    char seen[81];
    uint8_t reference[HOME_EYES_PIXELS], candidate[HOME_EYES_PIXELS];
} home_eyes_state_t;

void home_eyes_reset(home_eyes_state_t *s);
bool home_eyes_handles(const char *command);
cJSON *home_eyes_run(home_eyes_state_t *s, const char *command, const cJSON *params,
                     int64_t now_ms, int64_t epoch, bool *changed, bool *chime);
void home_eyes_register(cJSON *commands);
bool home_eyes_expire(home_eyes_state_t *s, int64_t epoch);
bool home_eyes_due(const home_eyes_state_t *s, int64_t now_ms);
bool home_eyes_frame(home_eyes_state_t *s, const uint8_t *frame, int64_t now_ms);
unsigned home_eyes_difference(const uint8_t *a, const uint8_t *b);
bool home_eyes_ack(home_eyes_state_t *s, uint32_t card_id);
bool home_eyes_reminder(home_eyes_state_t *s, int64_t now_ms);
char *home_eyes_save(const home_eyes_state_t *s, int64_t now_ms, int64_t epoch);
bool home_eyes_restore(home_eyes_state_t *s, const char *json, int64_t now_ms, int64_t epoch);
