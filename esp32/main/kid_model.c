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

#include "kid_model.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const cJSON *get(const cJSON *obj, const char *key) { return cJSON_GetObjectItemCaseSensitive(obj, key); }
bool kid_model_clock_valid(int64_t now) { return now >= 1704067200LL && now <= 4102444799LL; }

static bool fields(const cJSON *obj, const char *allowed) {
    if (!cJSON_IsObject(obj)) return false;
    const cJSON *v;
    cJSON_ArrayForEach(v, obj) {
        char key[64];
        if (!v->string || strlen(v->string) > 58) return false;
        snprintf(key, sizeof(key), "|%s|", v->string);
        if (!strstr(allowed, key)) return false;
        for (const cJSON *next = v->next; next; next = next->next) {
            if (!strcmp(v->string, next->string)) return false;
        }
    }
    return true;
}

// Accepts one line in the generated font's character set.
// 接收生成字体字符集中的单行文本。
static bool text(const cJSON *v, char *out, size_t capacity, bool empty) {
    if (!cJSON_IsString(v) || !v->valuestring) return false;
    size_t length = strlen(v->valuestring);
    if (length >= capacity || (!empty && !length)) return false;
    const unsigned char *p = (const unsigned char *)v->valuestring;
    while (*p) {
        uint32_t cp = *p++;
        if (cp < 128) { if (cp < 32 || cp == 127) return false; }
        else {
            int n = cp >= 0xC2 && cp <= 0xDF ? 1 : cp >= 0xE0 && cp <= 0xEF ? 2 : 0;
            if (!n) return false;
            cp &= (1u << (6 - n)) - 1;
            for (int i = 0; i < n; i++) {
                if (!*p || (*p & 0xC0) != 0x80) return false;
                cp = (cp << 6) | (*p++ & 63);
            }
            if (cp < (n == 1 ? 0x80u : 0x800u) || card_font_glyph(card_font_find(28), cp)->codepoint != cp) return false;
        }
    }
    memcpy(out, v->valuestring, length + 1); return true;
}

static bool identifier(const cJSON *v, char *out, size_t capacity) {
    if (!text(v, out, capacity, false)) return false;
    for (const unsigned char *p = (const unsigned char *)out; *p; p++) {
        if (!(*p >= 'A' && *p <= 'Z') && !(*p >= 'a' && *p <= 'z')
            && !(*p >= '0' && *p <= '9') && *p != '_' && *p != '-') return false;
    }
    return true;
}

static bool integer(const cJSON *v, int low, int high) {
    return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble >= low
        && v->valuedouble <= high && floor(v->valuedouble) == v->valuedouble;
}

static bool hhmm(const cJSON *v, kid_item_t *item) {
    if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) != 5) return false;
    const char *p = v->valuestring;
    if (p[2] != ':' || !isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])
        || !isdigit((unsigned char)p[3]) || !isdigit((unsigned char)p[4])) return false;
    int h = (p[0] - '0') * 10 + p[1] - '0', m = (p[3] - '0') * 10 + p[4] - '0';
    if (h > 23 || m > 59) return false;
    memcpy(item->time, p, 6); item->minutes = h * 60 + m; return true;
}

static bool date(const cJSON *v, char out[11]) {
    if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) != 10) return false;
    const char *p = v->valuestring;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) { if (p[i] != '-') return false; }
        else if (!isdigit((unsigned char)p[i])) return false;
    }
    int y = (p[0]-'0')*1000+(p[1]-'0')*100+(p[2]-'0')*10+p[3]-'0';
    int m = (p[5]-'0')*10+p[6]-'0', d = (p[8]-'0')*10+p[9]-'0';
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (y < 2024 || y > 2099 || m < 1 || m > 12 || d < 1
        || d > days[m-1] + (m == 2 && y % 4 == 0)) return false;
    memcpy(out, p, 11); return true;
}

void kid_model_date(int64_t now, int offset, char out[11]) {
    out[0] = 0;
    time_t local = (time_t)(now + offset * 60LL); struct tm t;
    if (kid_model_clock_valid(now) && gmtime_r(&local, &t)) strftime(out, 11, "%Y-%m-%d", &t);
}

void kid_model_init(kid_model_t *m) {
    memset(m, 0, sizeof(*m)); strcpy(m->child, "My day");
    m->goal = 5; strcpy(m->reward_icon, "gift");
}

// Replaces a dated timeline and restores matching completions on the same date.
// 替换指定日期的时间线，并恢复同一天相同编号的完成情况。
static bool schedule(kid_model_t *m, const cJSON *params, int64_t now, int offset) {
    if (!fields(params, "|child||date||items|")) return false;
    if (get(params, "child") && !text(get(params, "child"), m->child, sizeof(m->child), false)) return false;
    char day[11];
    if (get(params, "date")) { if (!date(get(params, "date"), day)) return false; }
    else { kid_model_date(now, offset, day); if (!day[0]) return false; }
    bool same_date = !strcmp(day, m->date);
    const cJSON *items = get(params, "items");
    if (!cJSON_IsArray(items) || cJSON_GetArraySize(items) > KID_ITEMS) return false;
    kid_item_t old[KID_ITEMS]; memcpy(old, m->items, sizeof(old)); unsigned old_count = m->item_count;
    memset(m->items, 0, sizeof(m->items)); m->item_count = 0;
    const cJSON *v;
    cJSON_ArrayForEach(v, items) {
        kid_item_t *item = &m->items[m->item_count];
        if (!fields(v, "|id||time||label||icon||who|")
            || !identifier(get(v, "id"), item->id, sizeof(item->id)) || !hhmm(get(v, "time"), item)
            || !text(get(v, "label"), item->label, sizeof(item->label), false)
            || !identifier(get(v, "icon"), item->icon, sizeof(item->icon))
            || (get(v, "who") && !text(get(v, "who"), item->who, sizeof(item->who), true))) return false;
        for (unsigned j = 0; j < m->item_count; j++) if (!strcmp(m->items[j].id, item->id)) return false;
        for (unsigned j = 0; same_date && j < old_count; j++) {
            if (!strcmp(old[j].id, item->id)) { item->done = old[j].done; item->done_at = old[j].done_at; }
        }
        m->item_count++;
    }
    // Stable time ordering preserves the parent's order for simultaneous steps.
    // 稳定时间排序保留家长为同一时刻步骤指定的顺序。
    for (unsigned i = 1; i < m->item_count; i++) {
        kid_item_t item = m->items[i]; unsigned j = i;
        while (j && m->items[j-1].minutes > item.minutes) { m->items[j] = m->items[j-1]; j--; }
        m->items[j] = item;
    }
    strcpy(m->date, day); return true;
}

static bool stars(kid_model_t *m, const cJSON *params) {
    if (!fields(params, "|count||goal||reward_label||reward_icon||note|")
        || !integer(get(params, "count"), 0, 999) || !integer(get(params, "goal"), 1, KID_STAR_GOAL)) return false;
    m->count = (int)get(params, "count")->valuedouble; m->goal = (int)get(params, "goal")->valuedouble;
    return (!get(params, "reward_label") || text(get(params, "reward_label"), m->reward_label, sizeof(m->reward_label), true))
        && (!get(params, "reward_icon") || identifier(get(params, "reward_icon"), m->reward_icon, sizeof(m->reward_icon)))
        && (!get(params, "note") || text(get(params, "note"), m->note, sizeof(m->note), true));
}

bool kid_model_command(kid_model_t *m, const char *command, const cJSON *params,
                        int64_t now, int offset, bool *changed, const char **error) {
    *changed = false; *error = "Check fields, text limits, date, HH:MM, unique IDs and numeric limits";
    kid_model_t before = *m;
    bool ok = false;
    if (!strcmp(command, "kid.set_schedule")) ok = schedule(m, params, now, offset);
    else if (!strcmp(command, "kid.set_stars")) ok = stars(m, params);
    else if (!strcmp(command, "kid.mark")) {
        char id[32];
        if (fields(params, "|id||done|") && identifier(get(params, "id"), id, sizeof(id)) && cJSON_IsBool(get(params, "done"))) {
            for (unsigned i = 0; i < m->item_count; i++) if (!strcmp(id, m->items[i].id)) {
                bool done = cJSON_IsTrue(get(params, "done"));
                if (done != m->items[i].done) {
                    m->items[i].done = done; m->items[i].done_at = done && kid_model_clock_valid(now) ? now : 0;
                }
                ok = true; break;
            }
        }
    } else if (!strcmp(command, "kid.state")) {
        ok = fields(params, "|") && !params->child;
        if (ok) { *error = NULL; return true; }
    }
    if (!ok) { *m = before; return false; }
    m->active = true; *changed = memcmp(&before, m, sizeof(*m)) != 0; *error = NULL; return true;
}

// Advances an earlier plan to the current local date with unfinished steps.
// 将较早的计划顺延至当前本地日期，并重置步骤完成情况。
bool kid_model_rollover(kid_model_t *m, int64_t now, int offset) {
    char day[11]; kid_model_date(now, offset, day);
    if (!m->date[0] || !day[0] || strcmp(day, m->date) <= 0) return false;
    strcpy(m->date, day);
    for (unsigned i = 0; i < m->item_count; i++) { m->items[i].done = false; m->items[i].done_at = 0; }
    return true;
}

int kid_model_current(const kid_model_t *m, int64_t now, int offset) {
    char day[11]; kid_model_date(now, offset, day);
    if (!day[0] || strcmp(day, m->date)) return -1;
    int minutes = (int)((now + offset * 60LL) % 86400 / 60), due = -1, next = -1;
    for (unsigned i = 0; i < m->item_count; i++) {
        if (m->items[i].done) continue;
        if (next < 0 || m->items[i].minutes < m->items[next].minutes) next = (int)i;
        if (m->items[i].minutes <= minutes && (due < 0 || m->items[i].minutes < m->items[due].minutes)) due = (int)i;
    }
    return due >= 0 ? due : next;
}

// Queues a completion notice and marks the selected step after acceptance.
// 完成通知入队成功后，标记所选步骤完成。
bool kid_model_green(kid_model_t *m, int64_t now, int offset, const kid_hooks_t *hooks) {
    int current = kid_model_current(m, now, offset);
    if (current < 0 || !hooks || !hooks->event) return false;
    kid_item_t *item = &m->items[current];
    char summary[192]; snprintf(summary, sizeof(summary), "%s confirmed \"%s\" (%s).", m->child, item->label, item->time);
    cJSON *data = cJSON_CreateObject(); if (!data) return false;
    cJSON_AddStringToObject(data, "type", "kid.done"); cJSON_AddStringToObject(data, "id", item->id);
    cJSON_AddNumberToObject(data, "at", (double)now);
    bool queued = hooks->event(summary, data); cJSON_Delete(data);
    if (!queued) return false;
    item->done = true; item->done_at = now;
    if (hooks->chime) hooks->chime(false);
    return true;
}

static cJSON *item_json(const kid_item_t *item) {
    cJSON *v = cJSON_CreateObject();
    cJSON_AddStringToObject(v, "id", item->id); cJSON_AddStringToObject(v, "time", item->time);
    cJSON_AddStringToObject(v, "label", item->label); cJSON_AddStringToObject(v, "icon", item->icon);
    cJSON_AddStringToObject(v, "who", item->who); cJSON_AddBoolToObject(v, "done", item->done);
    cJSON_AddNumberToObject(v, "done_at", (double)item->done_at); return v;
}

cJSON *kid_model_json(const kid_model_t *m, int64_t now, int offset) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "child", m->child); cJSON_AddStringToObject(root, "date", m->date);
    cJSON *items = cJSON_AddArrayToObject(root, "items");
    for (unsigned i = 0; i < m->item_count; i++) cJSON_AddItemToArray(items, item_json(&m->items[i]));
    int current = kid_model_current(m, now, offset);
    if (current >= 0) cJSON_AddItemToObject(root, "current", item_json(&m->items[current]));
    else cJSON_AddNullToObject(root, "current");
    cJSON *s = cJSON_AddObjectToObject(root, "stars");
    cJSON_AddNumberToObject(s, "count", m->count); cJSON_AddNumberToObject(s, "goal", m->goal);
    cJSON_AddBoolToObject(s, "goal_reached", m->count >= m->goal);
    cJSON_AddStringToObject(s, "reward_label", m->reward_label); cJSON_AddStringToObject(s, "reward_icon", m->reward_icon);
    cJSON_AddStringToObject(s, "note", m->note);
    cJSON_AddBoolToObject(root, "clock_valid", kid_model_clock_valid(now));
    cJSON_AddNumberToObject(root, "now_epoch", (double)now); cJSON_AddNumberToObject(root, "utc_offset_min", offset);
    return root;
}

void kid_refresh_request(kid_refresh_t *r) { r->dirty = true; }
int64_t kid_refresh_wait(const kid_refresh_t *r, int64_t us) {
    if (!r->dirty) return -1;
    int64_t wait = r->started ? r->last_us + KID_REFRESH_US - us : 0;
    return wait > 0 ? wait : 0;
}
void kid_refresh_started(kid_refresh_t *r, int64_t us) { r->started = true; r->last_us = us; r->dirty = false; }
