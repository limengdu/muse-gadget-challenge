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

#include "door_sign_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const s_states[] = {"meeting", "focus", "free", "away", "custom"};
static const char *const s_titles[] = {"In a meeting", "Focusing", "Come in", "Away", "My door"};
static const uint32_t s_colors[] = {0xb52b36, 0x9b6500, 0x167347, 0x245eaa, 0x6943a3};
static const char *const s_replies[DOOR_SIGN_STATE_COUNT][DOOR_SIGN_REPLIES] = {
    {"Dinner is ready", "Need you", "Parcel arrived"},
    {"Need you", "Dinner is ready", "Parcel arrived"},
    {"Come here", "Dinner is ready", "Parcel arrived"},
    {"Parcel arrived", "Call me", "I'm home"},
    {"Need you", "Dinner is ready", "Parcel arrived"},
};

const char *door_sign_state_name(door_sign_state_t state)
{
    return state >= 0 && state < DOOR_SIGN_STATE_COUNT ? s_states[state] : "unknown";
}

void door_sign_defaults(door_sign_model_t *out, door_sign_state_t state)
{
    memset(out, 0, sizeof(*out));
    out->active = true;
    out->state = state;
    out->color = s_colors[state];
    strcpy(out->title, s_titles[state]);
    strcpy(out->owner, "Dad");
    out->reply_count = DOOR_SIGN_REPLIES;
    for (unsigned i = 0; i < DOOR_SIGN_REPLIES; i++) strcpy(out->replies[i], s_replies[state][i]);
}

// Validate bounded printable UTF-8, including complete multi-byte sequences.
// 校验长度受限的可打印 UTF-8 文本及完整的多字节字符。
static bool text(const cJSON *value, char *out, size_t cap, bool empty)
{
    if (!cJSON_IsString(value) || !value->valuestring) return false;
    const unsigned char *s = (const unsigned char *)value->valuestring;
    size_t n = strlen((const char *)s);
    if (n >= cap || (!n && !empty)) return false;
    bool visible = empty;
    for (size_t i = 0; i < n;) {
        uint32_t c = s[i++];
        if (c < 0x20 || c == 0x7f) return false;
        if (c > 0x20) visible = true;
        if (c < 0x80) continue;
        unsigned more;
        uint32_t minimum;
        if (c >= 0xc2 && c <= 0xdf) { more = 1; c &= 0x1f; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { more = 2; c &= 0x0f; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { more = 3; c &= 7; minimum = 0x10000; }
        else return false;
        if (i + more > n) return false;
        while (more--) {
            if ((s[i] & 0xc0) != 0x80) return false;
            c = (c << 6) | (s[i++] & 0x3f);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)
            || (c >= 0x80 && c <= 0x9f) || c == 0x2028 || c == 0x2029) return false;
    }
    if (!visible) return false;
    memcpy(out, s, n + 1);
    return true;
}

static bool color(const cJSON *value, uint32_t *out)
{
    if (!cJSON_IsString(value) || !value->valuestring) return false;
    const char *s = value->valuestring;
    if (strlen(s) != 7 || s[0] != '#') return false;
    uint32_t c = 0;
    for (int i = 1; i <= 6; i++) {
        char ch = s[i];
        int digit = ch >= '0' && ch <= '9' ? ch - '0' :
                    ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 :
                    ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
        if (digit < 0) return false;
        c = c * 16 + (unsigned)digit;
    }
    *out = c;
    return true;
}

static bool parse(const cJSON *params, door_sign_time_t now, bool restore,
                  door_sign_model_t *out, const char **error)
{
    *error = "Invalid sign parameters";
    if (!cJSON_IsObject(params)) return false;
    static const char *const keys[] = {"state", "title", "subtitle", "until", "color", "owner", "replies"};
    unsigned seen = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, params) {
        unsigned i;
        for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
            if (item->string && !strcmp(item->string, keys[i])) break;
        if (i == sizeof(keys) / sizeof(keys[0]) || (seen & (1u << i))) return false;
        seen |= 1u << i;
    }
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(params, "state");
    if (!cJSON_IsString(state) || !state->valuestring) return false;
    int index;
    for (index = 0; index < DOOR_SIGN_STATE_COUNT; index++)
        if (!strcmp(state->valuestring, s_states[index])) break;
    if (index == DOOR_SIGN_STATE_COUNT) return false;
    door_sign_model_t next;
    door_sign_defaults(&next, (door_sign_state_t)index);
    struct { const char *name; char *out; size_t cap; bool empty; } fields[] = {
        {"title", next.title, sizeof(next.title), false},
        {"subtitle", next.subtitle, sizeof(next.subtitle), true},
        {"owner", next.owner, sizeof(next.owner), false},
    };
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        item = cJSON_GetObjectItemCaseSensitive(params, fields[i].name);
        if (item && !text(item, fields[i].out, fields[i].cap, fields[i].empty)) return false;
    }
    item = cJSON_GetObjectItemCaseSensitive(params, "color");
    if (item && (index != DOOR_SIGN_CUSTOM || !color(item, &next.color))) return false;
    item = cJSON_GetObjectItemCaseSensitive(params, "replies");
    if (item) {
        if (!cJSON_IsArray(item) || cJSON_GetArraySize(item) > DOOR_SIGN_REPLIES) return false;
        next.reply_count = (unsigned)cJSON_GetArraySize(item);
        memset(next.replies, 0, sizeof(next.replies));
        for (unsigned i = 0; i < next.reply_count; i++) {
            if (!text(cJSON_GetArrayItem(item, (int)i), next.replies[i], sizeof(next.replies[i]), false)) return false;
            for (unsigned j = 0; j < i; j++) if (!strcmp(next.replies[i], next.replies[j])) return false;
        }
    }
    item = cJSON_GetObjectItemCaseSensitive(params, "until");
    if (item) {
        *error = "until needs a valid clock and a future epoch or local HH:MM";
        if (!restore && !now.valid) return false;
        if (cJSON_IsNumber(item)) {
            double v = item->valuedouble;
            if (!isfinite(v) || floor(v) != v || v < 1704067200LL || v > 4102444799LL
                || (!restore && v <= now.epoch)) return false;
            next.until = (int64_t)v;
        } else if (!restore && cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) == 5) {
            const char *s = item->valuestring;
            if (s[0] < '0' || s[0] > '2' || s[1] < '0' || s[1] > '9' || s[2] != ':'
                || s[3] < '0' || s[3] > '5' || s[4] < '0' || s[4] > '9') return false;
            int hour = (s[0] - '0') * 10 + s[1] - '0';
            if (hour > 23 || now.local_second < 0 || now.local_second >= 86400) return false;
            int delta = hour * 3600 + ((s[3] - '0') * 10 + s[4] - '0') * 60 - now.local_second;
            if (delta <= 0) delta += 86400;
            next.until = now.epoch + delta;
            if (next.until > 4102444799LL) return false;
        } else return false;
    }
    *out = next;
    *error = NULL;
    return true;
}

bool door_sign_parse(const cJSON *params, door_sign_time_t now,
                     door_sign_model_t *out, const char **error)
{
    return parse(params, now, false, out, error);
}

cJSON *door_sign_json(const door_sign_model_t *sign)
{
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "state", door_sign_state_name(sign->state));
    cJSON_AddStringToObject(out, "title", sign->title);
    cJSON_AddStringToObject(out, "subtitle", sign->subtitle);
    cJSON_AddStringToObject(out, "owner", sign->owner);
    if (sign->until) cJSON_AddNumberToObject(out, "until", (double)sign->until);
    if (sign->state == DOOR_SIGN_CUSTOM) {
        char hex[8];
        snprintf(hex, sizeof(hex), "#%06lx", (unsigned long)sign->color);
        cJSON_AddStringToObject(out, "color", hex);
    }
    cJSON *replies = cJSON_AddArrayToObject(out, "replies");
    for (unsigned i = 0; i < sign->reply_count; i++) cJSON_AddItemToArray(replies, cJSON_CreateString(sign->replies[i]));
    return out;
}

char *door_sign_encode(const door_sign_model_t *sign)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddBoolToObject(root, "active", sign->active);
    cJSON_AddItemToObject(root, "sign", door_sign_json(sign));
    char *stored = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return stored;
}

// Restore versioned state, including deadlines that elapsed while powered off.
// 恢复带版本号的状态，包括断电期间已经到期的截止时间。
bool door_sign_decode(const char *stored, door_sign_model_t *out)
{
    if (!stored || strlen(stored) >= DOOR_SIGN_STORAGE_BYTES) return false;
    cJSON *root = cJSON_ParseWithOpts(stored, NULL, true);
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *active = cJSON_GetObjectItemCaseSensitive(root, "active");
    const char *error;
    door_sign_model_t next;
    bool ok = cJSON_IsObject(root) && cJSON_IsNumber(version) && version->valuedouble == 1
        && cJSON_IsBool(active) && parse(cJSON_GetObjectItemCaseSensitive(root, "sign"),
                                       (door_sign_time_t){0}, true, &next, &error);
    if (ok) { next.active = cJSON_IsTrue(active); *out = next; }
    cJSON_Delete(root);
    return ok;
}

bool door_sign_expire(door_sign_model_t *sign, door_sign_time_t now)
{
    if (!sign->active || !sign->until || !now.valid || now.epoch < sign->until) return false;
    char owner[DOOR_SIGN_OWNER_BYTES];
    strcpy(owner, sign->owner);
    door_sign_defaults(sign, DOOR_SIGN_FREE);
    strcpy(sign->owner, owner);
    return true;
}

// Accept one reply across all buttons per ten seconds and format its event.
// 所有按钮合计每十秒接受一次回复，并生成对应事件文本。
bool door_sign_tap(const door_sign_model_t *sign, unsigned index, int64_t now_us,
                   int64_t *last_us, door_sign_time_t now, char summary[256])
{
    if (!sign->active || index >= sign->reply_count || now_us < 0
        || (*last_us >= 0 && now_us - *last_us < 10000000)) return false;
    char stamp[24] = "";
    if (now.valid) snprintf(stamp, sizeof(stamp), " (%02d:%02d)", now.local_second / 3600,
                            now.local_second / 60 % 60);
    snprintf(summary, 256, "Someone at %s's door tapped \"%s\"%s.", sign->owner, sign->replies[index], stamp);
    *last_us = now_us;
    return true;
}
