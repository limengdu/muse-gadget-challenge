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

#include "care_model.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MIN_EPOCH 1704067200LL
#define MAX_EPOCH 4102444799LL
static const char *const statuses[] = {"pending", "due", "done", "missed"};

static bool valid_clock(int64_t now) { return now >= MIN_EPOCH && now <= MAX_EPOCH; }
static int64_t local_day(int64_t now, int offset) { return (now + offset * 60LL) / 86400; }
static bool number(const cJSON *v, double low, double high, bool integer) {
    return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble >= low
        && v->valuedouble <= high && (!integer || floor(v->valuedouble) == v->valuedouble);
}

// Accepts the characters present in the card font, preserving UTF-8 punctuation.
// 接收卡片字体已包含的字符，并保留 UTF-8 标点。
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
    memcpy(out, v->valuestring, length + 1);
    return true;
}

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

static const cJSON *get(const cJSON *obj, const char *key) { return cJSON_GetObjectItemCaseSensitive(obj, key); }
static bool hhmm(const cJSON *v, char out[6], int *minutes) {
    if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) != 5) return false;
    const char *p = v->valuestring;
    if (p[2] != ':' || !isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])
        || !isdigit((unsigned char)p[3]) || !isdigit((unsigned char)p[4])) return false;
    int h = (p[0] - '0') * 10 + p[1] - '0', m = (p[3] - '0') * 10 + p[4] - '0';
    if (h > 23 || m > 59) return false;
    memcpy(out, p, 6); *minutes = h * 60 + m; return true;
}

static int64_t scheduled(int minutes, int64_t now, int offset) {
    if (!valid_clock(now)) return 0;
    int64_t at = local_day(now, offset) * 86400 - offset * 60LL + minutes * 60;
    return at < now ? at + 86400 : at;
}

// Rebases pending local-time deadlines when the UTC offset changes.
// UTC 时差变化时重新计算待办提醒的本地时间截止点。
static bool rebase_offset(care_model_t *m, int64_t now, int offset) {
    if (m->schedule_offset == offset) return false;
    m->schedule_offset = offset;
    if (valid_clock(now)) m->day = local_day(now, offset);
    for (unsigned i = 0; i < m->reminder_count; i++) {
        care_reminder_t *q = &m->reminders[i];
        if (q->status == CARE_PENDING) q->next_due = scheduled(q->minutes, now, offset);
    }
    return true;
}

void care_model_init(care_model_t *m) {
    memset(m, 0, sizeof(*m));
    strcpy(m->person, "Dad"); strcpy(m->family, "Family");
    strcpy(m->title, "Care Board"); strcpy(m->footer, "Ready for your family's messages");
    m->day = -1;
}

static bool message(care_message_t *m, const cJSON *v, int64_t now, bool added) {
    if (!fields(v, added ? "|from||text|" : "|from||text||time||read|")) return false;
    memset(m, 0, sizeof(*m));
    if (!text(get(v, "from"), m->from, sizeof(m->from), false)
        || !text(get(v, "text"), m->text, sizeof(m->text), false)) return false;
    const cJSON *time = get(v, "time"), *read = get(v, "read");
    if (time && !number(time, 0, MAX_EPOCH, true)) return false;
    if (read && !cJSON_IsBool(read)) return false;
    m->time = time ? (int64_t)time->valuedouble : valid_clock(now) ? now : 0;
    m->read = cJSON_IsTrue(read);
    return true;
}

bool care_model_patch(care_model_t *m, const cJSON *patch, int64_t now, int offset,
                       bool *new_message, const char **error) {
    *new_message = false;
    *error = "Invalid care patch: check fields, text limits, HH:MM, icon names and array limits";
    if (!fields(patch, "|now_epoch||utc_offset_min||person||family||title||weather||messages||add_message||reminders|")) return false;
    const cJSON *epoch = get(patch, "now_epoch"), *zone = get(patch, "utc_offset_min");
    if (epoch || zone) {
        if (!number(epoch, MIN_EPOCH, MAX_EPOCH, true) || !number(zone, -840, 840, true)) return false;
        now = (int64_t)epoch->valuedouble; offset = (int)zone->valuedouble;
    }
    rebase_offset(m, now, offset);
    const cJSON *v = get(patch, "person");
    if (v && !text(v, m->person, sizeof(m->person), false)) return false;
    v = get(patch, "family");
    if (v && !text(v, m->family, sizeof(m->family), false)) return false;
    v = get(patch, "title");
    if (v && !text(v, m->title, sizeof(m->title), true)) return false;
    v = get(patch, "weather");
    if (v) {
        if (!fields(v, "|text||icon||temp|")
            || !text(get(v, "text"), m->weather_text, sizeof(m->weather_text), true)
            || !text(get(v, "icon"), m->weather_icon, sizeof(m->weather_icon), false)
            || !card_icon_find(m->weather_icon, 64) || !number(get(v, "temp"), -80, 80, false)) return false;
        m->weather_temp = get(v, "temp")->valuedouble; m->weather_set = true;
    }
    v = get(patch, "messages");
    if (v) {
        if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) > CARE_MESSAGES || get(patch, "add_message")) return false;
        memset(m->messages, 0, sizeof(m->messages)); m->message_count = 0;
        const cJSON *item;
        cJSON_ArrayForEach(item, v) {
            if (!message(&m->messages[m->message_count++], item, now, false)) return false;
        }
        for (unsigned i = 1; i < m->message_count; i++) {
            care_message_t item = m->messages[i]; unsigned j = i;
            while (j && m->messages[j - 1].time < item.time) { m->messages[j] = m->messages[j - 1]; j--; }
            m->messages[j] = item;
        }
    }
    v = get(patch, "add_message");
    if (v) {
        care_message_t item;
        if (!message(&item, v, now, true)) return false;
        memmove(m->messages + 1, m->messages, (CARE_MESSAGES - 1) * sizeof(item));
        m->messages[0] = item;
        if (m->message_count < CARE_MESSAGES) m->message_count++;
        *new_message = true;
    }
    v = get(patch, "reminders");
    if (v) {
        if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) > CARE_REMINDERS) return false;
        care_reminder_t old[CARE_REMINDERS];
        memcpy(old, m->reminders, sizeof(old)); unsigned old_count = m->reminder_count;
        m->reminder_count = 0; memset(m->reminders, 0, sizeof(m->reminders));
        const cJSON *item;
        cJSON_ArrayForEach(item, v) {
            care_reminder_t *q = &m->reminders[m->reminder_count];
            if (!fields(item, "|id||time||label||icon||repeat||escalate_min|")
                || !text(get(item, "id"), q->id, sizeof(q->id), false)
                || !text(get(item, "label"), q->label, sizeof(q->label), false)
                || !hhmm(get(item, "time"), q->time, &q->minutes)) return false;
            for (const char *p = q->id; *p; p++) if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') return false;
            for (unsigned i = 0; i < m->reminder_count; i++) if (!strcmp(q->id, m->reminders[i].id)) return false;
            const cJSON *repeat = get(item, "repeat");
            if (!cJSON_IsString(repeat) || (strcmp(repeat->valuestring, "daily") && strcmp(repeat->valuestring, "once"))) return false;
            q->daily = !strcmp(repeat->valuestring, "daily");
            strcpy(q->icon, "alarm");
            if (get(item, "icon") && (!text(get(item, "icon"), q->icon, sizeof(q->icon), false) || !card_icon_find(q->icon, 64))) return false;
            const cJSON *escalate = get(item, "escalate_min");
            if (escalate && !number(escalate, 1, 1440, true)) return false;
            q->escalate_min = escalate ? (int)escalate->valuedouble : 30;
            q->next_due = scheduled(q->minutes, now, offset);
            for (unsigned i = 0; i < old_count; i++) if (!strcmp(q->id, old[i].id) && !strcmp(q->time, old[i].time)) {
                q->status = old[i].status; q->due_at = old[i].due_at; q->done_at = old[i].done_at;
                q->next_due = old[i].next_due; q->chimes = old[i].chimes; q->next_chime = old[i].next_chime;
            }
            m->reminder_count++;
        }
    }
    if (m->day < 0 && valid_clock(now)) m->day = local_day(now, offset);
    m->active = true; m->updated_at = valid_clock(now) ? now : 0;
    *error = NULL;
    return true;
}

void care_model_time(int64_t epoch, int offset, char out[6]) {
    if (!valid_clock(epoch)) { strcpy(out, "--:--"); return; }
    time_t at = (time_t)(epoch + offset * 60LL); struct tm t;
    if (!gmtime_r(&at, &t)) { strcpy(out, "--:--"); return; }
    snprintf(out, 6, "%02d:%02d", t.tm_hour, t.tm_min);
}

static bool emit(const care_model_t *m, const care_reminder_t *q, const care_message_t *msg,
                  const char *type, int64_t now, int offset, const care_hooks_t *hooks) {
    char summary[384], at[6]; care_model_time(now, offset, at);
    if (!strcmp(type, "due")) snprintf(summary, sizeof(summary), "\"%s\" is due now (%s).", q->label, q->time);
    else if (!strcmp(type, "missed")) snprintf(summary, sizeof(summary), "%s has not confirmed \"%s\" %d minutes after %s.", m->person, q->label, q->escalate_min, q->time);
    else if (!strcmp(type, "ack")) snprintf(summary, sizeof(summary), "%s confirmed \"%s\" (due %s) at %s.", m->person, q->label, q->time, at);
    else if (!strcmp(type, "read")) snprintf(summary, sizeof(summary), "%s read the message from %s.", m->person, msg->from);
    else if (!strcmp(type, "checkin")) snprintf(summary, sizeof(summary), "%s checked in: I'm OK today.", m->person);
    else snprintf(summary, sizeof(summary), "%s asked %s to call.", m->person, m->family);
    cJSON *data = cJSON_CreateObject();
    if (!data) return false;
    cJSON_AddStringToObject(data, "type", type); cJSON_AddStringToObject(data, "person", m->person);
    cJSON_AddStringToObject(data, "family", m->family); cJSON_AddNumberToObject(data, "at", (double)now);
    cJSON_AddStringToObject(data, "local_time", at);
    if (q) {
        cJSON_AddStringToObject(data, "id", q->id); cJSON_AddStringToObject(data, "label", q->label);
        cJSON_AddStringToObject(data, "time", q->time); cJSON_AddNumberToObject(data, "due_at", (double)q->due_at);
        cJSON_AddNumberToObject(data, "escalate_min", q->escalate_min);
    }
    if (msg) { cJSON_AddStringToObject(data, "from", msg->from); cJSON_AddNumberToObject(data, "message_time", (double)msg->time); }
    bool ok = hooks->event(summary, data); cJSON_Delete(data); return ok;
}

// Daily rollover resets daily occurrences and removes completed one-time entries.
// 每日换日时重置每日提醒，并移除已结束的一次性提醒。
// Pending future occurrences keep their scheduled day.
// 尚未到期的待办提醒保留其计划日期。
int64_t care_model_step(care_model_t *m, int64_t now, int offset, const care_hooks_t *hooks, bool *changed) {
    *changed = false;
    if (!valid_clock(now)) return 0;
    if (rebase_offset(m, now, offset)) { m->updated_at = now; *changed = true; }
    int64_t day = local_day(now, offset);
    int64_t next = (day + 1) * 86400 - offset * 60LL;
    if (day != m->day) {
        bool forward = m->day >= 0 && day > m->day;
        for (unsigned i = 0; i < m->reminder_count;) {
            care_reminder_t *q = &m->reminders[i];
            if (forward && !q->daily && (q->status == CARE_DONE || q->status == CARE_MISSED)) {
                memmove(q, q + 1, (--m->reminder_count - i) * sizeof(*q));
                memset(&m->reminders[m->reminder_count], 0, sizeof(*q)); continue;
            }
            if (forward && q->daily) {
                int64_t start = day * 86400 - offset * 60LL;
                if (q->status != CARE_PENDING || q->next_due < start) q->next_due = start + q->minutes * 60;
                q->status = CARE_PENDING; q->due_at = q->done_at = 0; q->chimes = 0;
            }
            if (q->status == CARE_PENDING && (!q->next_due || day < m->day)) q->next_due = scheduled(q->minutes, now, offset);
            i++;
        }
        m->day = day; m->updated_at = now; *changed = true;
    }
    bool ring = false;
    for (unsigned i = 0; i < m->reminder_count; i++) {
        care_reminder_t *q = &m->reminders[i];
        if (q->status == CARE_PENDING) {
            if (!q->next_due) q->next_due = scheduled(q->minutes, now, offset);
            if (now >= q->next_due) {
                q->due_at = q->next_due;
                if (emit(m, q, NULL, "due", now, offset, hooks)) {
                    q->status = CARE_DUE; q->chimes = 0; q->next_chime = now; m->updated_at = now; *changed = true;
                } else { if (next > now + 1) next = now + 1; continue; }
            } else if (next > q->next_due) next = q->next_due;
        }
        if (q->status == CARE_DUE) {
            int64_t miss = q->due_at + q->escalate_min * 60LL;
            if (now >= miss) {
                if (emit(m, q, NULL, "missed", now, offset, hooks)) { q->status = CARE_MISSED; m->updated_at = now; *changed = true; }
                else if (next > now + 1) next = now + 1;
            } else {
                if (now >= q->next_chime && q->chimes < 3) {
                    q->chimes++; q->next_chime = now + 300; ring = true; *changed = true;
                }
                if (next > miss) next = miss;
                if (q->chimes < 3 && next > q->next_chime) next = q->next_chime;
            }
        }
    }
    if (ring) hooks->chime(true);
    return next;
}

int care_model_oldest(const care_model_t *m) {
    int oldest = -1;
    for (unsigned i = 0; i < m->reminder_count; i++) {
        if ((m->reminders[i].status == CARE_DUE || m->reminders[i].status == CARE_MISSED)
            && (oldest < 0 || m->reminders[i].due_at < m->reminders[oldest].due_at)) oldest = (int)i;
    }
    return oldest;
}

bool care_model_key(care_model_t *m, care_key_t key, int64_t now, int offset,
                     int64_t uptime_us, const care_hooks_t *hooks) {
    if (key < 0 || key >= CARE_KEYS || (m->key_seen[key] && uptime_us - m->last_key_us[key] < 2000000)) return false;
    char at[6]; care_model_time(now, offset, at); bool ok = true;
    int oldest = care_model_oldest(m);
    if (key == CARE_GREEN && oldest >= 0) {
        care_reminder_t *q = &m->reminders[oldest];
        ok = emit(m, q, NULL, "ack", now, offset, hooks);
        if (ok) { q->status = CARE_DONE; q->done_at = now; snprintf(m->footer, sizeof(m->footer), "Told %s at %s", m->family, at); }
    } else if (key == CARE_GREEN) {
        unsigned i = 0; while (i < m->message_count && m->messages[i].read) i++;
        if (i < m->message_count) {
            ok = emit(m, NULL, &m->messages[i], "read", now, offset, hooks);
            if (ok) { m->messages[i].read = true; snprintf(m->footer, sizeof(m->footer), "Told %s you read the message", m->family); }
        } else strcpy(m->footer, "Nothing to confirm");
    } else if (key == CARE_LEFT) {
        ok = emit(m, NULL, NULL, "checkin", now, offset, hooks);
        if (ok) { m->last_checkin = now; snprintf(m->footer, sizeof(m->footer), "Told %s you are OK today", m->family); }
    } else {
        ok = emit(m, NULL, NULL, "call_request", now, offset, hooks);
        if (ok) snprintf(m->footer, sizeof(m->footer), "Asked %s to call you", m->family);
    }
    if (!ok) { strcpy(m->footer, "Please press again to send"); m->footer_check = false; return false; }
    m->key_seen[key] = true; m->last_key_us[key] = uptime_us; m->updated_at = valid_clock(now) ? now : 0;
    m->key_time_epoch[key] = valid_clock(now);
    m->last_keys[key] = valid_clock(now) ? now : uptime_us / 1000000; m->footer_check = true;
    hooks->chime(false); return true;
}

cJSON *care_model_json(const care_model_t *m) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "person", m->person); cJSON_AddStringToObject(root, "family", m->family);
    cJSON_AddStringToObject(root, "title", m->title); cJSON_AddBoolToObject(root, "active", m->active);
    cJSON_AddStringToObject(root, "footer", m->footer); cJSON_AddNumberToObject(root, "updated_at", (double)m->updated_at);
    cJSON_AddNumberToObject(root, "last_checkin", (double)m->last_checkin);
    cJSON *keys = cJSON_AddObjectToObject(root, "last_keys");
    const char *names[] = {"green", "left", "right"};
    for (int i = 0; i < CARE_KEYS; i++) cJSON_AddNumberToObject(keys, names[i], (double)m->last_keys[i]);
    cJSON *basis = cJSON_AddObjectToObject(root, "key_time_basis");
    for (int i = 0; i < CARE_KEYS; i++) cJSON_AddStringToObject(basis, names[i], m->key_time_epoch[i] ? "epoch" : "uptime");
    if (m->weather_set) {
        cJSON *w = cJSON_AddObjectToObject(root, "weather");
        cJSON_AddStringToObject(w, "text", m->weather_text); cJSON_AddStringToObject(w, "icon", m->weather_icon);
        cJSON_AddNumberToObject(w, "temp", m->weather_temp);
    } else cJSON_AddNullToObject(root, "weather");
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    for (unsigned i = 0; i < m->message_count; i++) {
        const care_message_t *v = &m->messages[i]; cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "from", v->from); cJSON_AddStringToObject(item, "text", v->text);
        cJSON_AddNumberToObject(item, "time", (double)v->time); cJSON_AddBoolToObject(item, "read", v->read);
        cJSON_AddItemToArray(messages, item);
    }
    cJSON *reminders = cJSON_AddArrayToObject(root, "reminders");
    for (unsigned i = 0; i < m->reminder_count; i++) {
        const care_reminder_t *q = &m->reminders[i]; cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", q->id); cJSON_AddStringToObject(item, "time", q->time);
        cJSON_AddStringToObject(item, "label", q->label); cJSON_AddStringToObject(item, "icon", q->icon);
        cJSON_AddStringToObject(item, "repeat", q->daily ? "daily" : "once");
        cJSON_AddNumberToObject(item, "escalate_min", q->escalate_min);
        cJSON_AddStringToObject(item, "status", statuses[q->status]);
        cJSON_AddNumberToObject(item, "due_at", (double)q->due_at); cJSON_AddNumberToObject(item, "done_at", (double)q->done_at);
        cJSON_AddNumberToObject(item, "next_due", (double)q->next_due); cJSON_AddNumberToObject(item, "chimes", q->chimes);
        cJSON_AddItemToArray(reminders, item);
    }
    return root;
}
