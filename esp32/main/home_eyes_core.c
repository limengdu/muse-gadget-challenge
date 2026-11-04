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

#include "home_eyes_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cJSON *error(const char *message)
{
    cJSON *r = cJSON_CreateObject(), *e = cJSON_AddObjectToObject(r, "error");
    cJSON_AddBoolToObject(r, "ok", false);
    cJSON_AddStringToObject(e, "code", "invalid_params");
    cJSON_AddStringToObject(e, "message", message);
    return r;
}

static cJSON *success(void)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddObjectToObject(r, "payload");
    return r;
}

static bool string(const cJSON *v, size_t capacity)
{
    if (!cJSON_IsString(v) || !v->valuestring) return false;
    size_t n = strlen(v->valuestring);
    bool content = false;
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)v->valuestring[i] > 32) content = true;
        if ((unsigned char)v->valuestring[i] < 32 && v->valuestring[i] != '\n') return false;
    }
    return content && n < capacity;
}

static bool integer(const cJSON *v, double min, double max)
{
    return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble >= min
           && v->valuedouble <= max && floor(v->valuedouble) == v->valuedouble;
}

static bool fields(const cJSON *p, const char *allowed)
{
    if (!p) return true;
    if (!cJSON_IsObject(p)) return false;
    const cJSON *v;
    cJSON_ArrayForEach(v, p) {
        char key[64];
        if (!v->string || strlen(v->string) > 50) return false;
        snprintf(key, sizeof(key), "|%s|", v->string);
        if (!strstr(allowed, key)) return false;
        for (const cJSON *next = v->next; next; next = next->next)
            if (next->string && !strcmp(next->string, v->string)) return false;
    }
    return true;
}

void home_eyes_reset(home_eyes_state_t *s)
{
    memset(s, 0, sizeof(*s));
    s->interval_min = 10;
    s->sensitivity = 1;
}

bool home_eyes_handles(const char *command)
{
    return command && (!strcmp(command, "watch.start") || !strcmp(command, "watch.stop")
        || !strcmp(command, "watch.status") || !strcmp(command, "watch.verdict")
        || !strcmp(command, "home.say"));
}

bool home_eyes_expire(home_eyes_state_t *s, int64_t epoch)
{
    if (!s->active || !s->until || !epoch || epoch < s->until) return false;
    s->active = s->waiting = false;
    s->events &= ~HOME_EYES_CHANGE;
    return true;
}

// Validate a complete command before applying its state transition.
// 先校验完整命令，再更新对应状态。
cJSON *home_eyes_run(home_eyes_state_t *s, const char *command, const cJSON *p,
                     int64_t now_ms, int64_t epoch, bool *changed, bool *chime)
{
    *changed = *chime = false;
    if (!home_eyes_handles(command)) return error("unknown Home Eyes command");
    if (!strcmp(command, "watch.start")) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(p, "name");
        const cJSON *q = cJSON_GetObjectItemCaseSensitive(p, "question");
        const cJSON *i = cJSON_GetObjectItemCaseSensitive(p, "interval_min");
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(p, "sensitivity");
        const cJSON *until = cJSON_GetObjectItemCaseSensitive(p, "until");
        if (!fields(p, "|name||question||interval_min||sensitivity||until|")
            || !string(name, sizeof(s->name)) || !string(q, sizeof(s->question))
            || (i && !integer(i, 2, 120)) || (v && !string(v, 8))
            || (until && (!epoch || !integer(until, epoch + 1, 4102444799.))))
            return error("name (1..48 bytes), question (1..192 bytes), interval_min (2..120 integer), sensitivity and future UTC until are required in range");
        unsigned sensitivity = 1;
        if (v) {
            if (!strcmp(v->valuestring, "low")) sensitivity = 0;
            else if (!strcmp(v->valuestring, "high")) sensitivity = 2;
            else if (strcmp(v->valuestring, "medium")) return error("sensitivity must be low, medium or high");
        }
        if (s->waiting) return error("finish the pending watch.verdict or watch.stop first");
        strcpy(s->name, name->valuestring);
        strcpy(s->question, q->valuestring);
        s->interval_min = i ? (unsigned)i->valuedouble : 10;
        s->sensitivity = sensitivity;
        s->until = until ? (int64_t)until->valuedouble : 0;
        s->active = true;
        s->reference_valid = s->candidate_valid = false;
        s->next_ms = now_ms;
        *changed = true;
    } else if (!strcmp(command, "watch.stop")) {
        if (!fields(p, "")) return error("watch.stop takes no parameters");
        s->active = s->waiting = false;
        s->events &= ~HOME_EYES_CHANGE;
        *changed = true;
    } else if (!strcmp(command, "watch.verdict")) {
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(p, "text");
        const cJSON *alert = cJSON_GetObjectItemCaseSensitive(p, "alert");
        if (!fields(p, "|text||alert|") || !string(text, sizeof(s->verdict_text)) || !cJSON_IsBool(alert))
            return error("text (1..480 bytes) and boolean alert are required");
        if (!s->active || !s->waiting) return error("no watch is awaiting a verdict");
        if (s->alert) return error("the previous alert awaits a wheel acknowledgement");
        strcpy(s->verdict_text, text->valuestring);
        s->verdict = true;
        s->alert = cJSON_IsTrue(alert);
        s->verdict_id = ++s->card_seq;
        s->waiting = false;
        s->events &= ~HOME_EYES_CHANGE;
        if (s->candidate_valid) {
            memcpy(s->reference, s->candidate, sizeof(s->reference));
            s->reference_valid = true;
        } else s->reference_valid = false;
        s->candidate_valid = false;
        s->next_ms = now_ms + s->interval_min * 60000LL;
        s->alarm_ms = now_ms + 5000;
        *chime = *changed = true;
    } else if (!strcmp(command, "home.say")) {
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(p, "text");
        const cJSON *from = cJSON_GetObjectItemCaseSensitive(p, "from");
        if (!fields(p, "|text||from|") || !string(text, sizeof(s->text)) || !string(from, sizeof(s->from)))
            return error("text (1..480 bytes) and from (1..48 bytes) are required");
        if (s->message || (s->events & HOME_EYES_MESSAGE_ACK)) return error("the previous message is awaiting acknowledgement delivery");
        strcpy(s->text, text->valuestring);
        strcpy(s->from, from->valuestring);
        s->message = true;
        s->reminded = false;
        s->message_ms = now_ms;
        s->message_epoch = epoch;
        s->message_id = ++s->card_seq;
        *chime = *changed = true;
    } else if (!fields(p, "")) return error("watch.status takes no parameters");
    cJSON *r = success(), *out = cJSON_GetObjectItemCaseSensitive(r, "payload");
    cJSON_AddBoolToObject(out, "active", s->active);
    cJSON_AddBoolToObject(out, "awaiting_verdict", s->waiting);
    cJSON_AddStringToObject(out, "name", s->name);
    cJSON_AddStringToObject(out, "question", s->question);
    cJSON_AddNumberToObject(out, "interval_min", s->interval_min);
    cJSON_AddStringToObject(out, "sensitivity", (const char *[]){"low", "medium", "high"}[s->sensitivity]);
    cJSON_AddNumberToObject(out, "until", (double)s->until);
    cJSON_AddNumberToObject(out, "next_check_s", s->active && !s->waiting ? (double)(s->next_ms > now_ms ? (s->next_ms - now_ms + 999) / 1000 : 0) : 0);
    cJSON_AddBoolToObject(out, "message_unseen", s->message);
    cJSON_AddBoolToObject(out, "alert_unacknowledged", s->alert);
    cJSON_AddStringToObject(out, "verdict", s->verdict_text);
    return r;
}

static unsigned blur(const uint8_t *p, int x, int y)
{
    unsigned sum = 0, n = 0;
    for (int j = y - 1; j <= y + 1; j++) for (int i = x - 1; i <= x + 1; i++) {
        if (i >= 0 && j >= 0 && i < HOME_EYES_SIDE && j < HOME_EYES_SIDE) {
            sum += p[j * HOME_EYES_SIDE + i]; n++;
        }
    }
    return (sum + n / 2) / n;
}

// Compare blurred luminance after removing the frame-wide exposure offset.
// 比较模糊后的亮度，并扣除整幅画面的曝光偏移。
unsigned home_eyes_difference(const uint8_t *a, const uint8_t *b)
{
    int offset = 0;
    for (int n = 0; n < HOME_EYES_PIXELS; n++) offset += (int)b[n] - a[n];
    offset /= HOME_EYES_PIXELS;
    unsigned sum = 0;
    for (int y = 0; y < HOME_EYES_SIDE; y++) for (int x = 0; x < HOME_EYES_SIDE; x++)
        sum += abs((int)blur(b, x, y) - (int)blur(a, x, y) - offset);
    return (sum + HOME_EYES_PIXELS / 2) / HOME_EYES_PIXELS;
}

bool home_eyes_due(const home_eyes_state_t *s, int64_t now_ms)
{
    return s->active && !s->waiting && !s->alert && now_ms >= s->next_ms;
}

bool home_eyes_frame(home_eyes_state_t *s, const uint8_t *frame, int64_t now_ms)
{
    if (!home_eyes_due(s, now_ms)) return false;
    s->next_ms = now_ms + s->interval_min * 60000LL;
    if (!s->reference_valid) {
        memcpy(s->reference, frame, sizeof(s->reference));
        s->reference_valid = true;
        return false;
    }
    const unsigned thresholds[] = {18, 10, 5};
    if (home_eyes_difference(s->reference, frame) < thresholds[s->sensitivity]) return false;
    memcpy(s->candidate, frame, sizeof(s->candidate));
    s->candidate_valid = s->waiting = true;
    s->events |= HOME_EYES_CHANGE;
    return true;
}

bool home_eyes_ack(home_eyes_state_t *s, uint32_t id)
{
    if (s->verdict && id == s->verdict_id) {
        if (s->alert) s->events |= HOME_EYES_ALERT_ACK;
        s->verdict = s->alert = false;
        return true;
    }
    if (s->message && id == s->message_id) {
        size_t n = strlen(s->text);
        if (n > 72) n = 72;
        while (n && ((unsigned char)s->text[n] & 0xc0) == 0x80) n--;
        memcpy(s->seen, s->text, n); s->seen[n] = 0;
        s->message = false;
        s->events |= HOME_EYES_MESSAGE_ACK;
        return true;
    }
    return false;
}

bool home_eyes_reminder(home_eyes_state_t *s, int64_t now_ms)
{
    if (!s->message || s->reminded || now_ms - s->message_ms < 600000) return false;
    s->reminded = true;
    return true;
}

// Persist text and scheduling state; thumbnails stay in volatile memory.
// 保存文字和调度状态；缩略图保留在易失内存中。
char *home_eyes_save(const home_eyes_state_t *s, int64_t now_ms, int64_t epoch)
{
    cJSON *r = cJSON_CreateObject();
#define NUM(k, v) cJSON_AddNumberToObject(r, k, (double)(v))
#define STR(k, v) cJSON_AddStringToObject(r, k, v)
    NUM("version", 1); NUM("active", s->active); NUM("waiting", s->waiting);
    NUM("interval", s->interval_min); NUM("sensitivity", s->sensitivity); NUM("until", s->until);
    NUM("next", epoch ? epoch + (s->next_ms > now_ms ? (s->next_ms - now_ms + 999) / 1000 : 0) : 0);
    NUM("message", s->message); NUM("verdict", s->verdict); NUM("alert", s->alert); NUM("reminded", s->reminded);
    NUM("message_epoch", s->message_epoch); NUM("events", s->events);
    NUM("card_seq", s->card_seq); NUM("message_id", s->message_id); NUM("verdict_id", s->verdict_id);
    NUM("message_age", (now_ms - s->message_ms) / 1000);
    STR("name", s->name); STR("question", s->question); STR("text", s->text);
    STR("from", s->from); STR("verdict_text", s->verdict_text); STR("seen", s->seen);
#undef NUM
#undef STR
    char *json = cJSON_PrintUnformatted(r); cJSON_Delete(r); return json;
}

bool home_eyes_restore(home_eyes_state_t *s, const char *json, int64_t now_ms, int64_t epoch)
{
    cJSON *r = cJSON_Parse(json);
    if (!cJSON_IsObject(r)) {
        cJSON_Delete(r);
        return false;
    }
    home_eyes_state_t *restored = calloc(1, sizeof(*restored));
    if (!restored) {
        cJSON_Delete(r);
        return false;
    }
    home_eyes_reset(restored);
#define LOAD(field, key, min, max) do { \
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(r, key); \
    if (!integer(v, min, max)) { \
        goto invalid; \
    } \
    restored->field = v->valuedouble; \
} while (0)
#define TEXT(field, key) do { \
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(r, key); \
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= sizeof(restored->field)) { \
        goto invalid; \
    } \
    strcpy(restored->field, v->valuestring); \
} while (0)
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(r, "version");
    if (!integer(ver, 1, 1)) goto invalid;
    LOAD(active, "active", 0, 1);
    LOAD(waiting, "waiting", 0, 1);
    LOAD(interval_min, "interval", 2, 120);
    LOAD(sensitivity, "sensitivity", 0, 2);
    LOAD(until, "until", 0, 4102444799.);
    LOAD(message, "message", 0, 1);
    LOAD(verdict, "verdict", 0, 1);
    LOAD(alert, "alert", 0, 1);
    LOAD(reminded, "reminded", 0, 1);
    LOAD(message_epoch, "message_epoch", 0, 4102444799.);
    LOAD(events, "events", 0, 7);
    LOAD(card_seq, "card_seq", 0, UINT32_MAX);
    LOAD(message_id, "message_id", 0, UINT32_MAX);
    LOAD(verdict_id, "verdict_id", 0, UINT32_MAX);
    TEXT(name, "name");
    TEXT(question, "question");
    TEXT(text, "text");
    TEXT(from, "from");
    TEXT(verdict_text, "verdict_text");
    TEXT(seen, "seen");
    const cJSON *next = cJSON_GetObjectItemCaseSensitive(r, "next");
    const cJSON *age = cJSON_GetObjectItemCaseSensitive(r, "message_age");
    if (!integer(next, 0, 4102451999.) || !integer(age, 0, 4102444799.)) goto invalid;
    restored->next_ms = now_ms + (epoch && next->valuedouble > epoch ?
        (int64_t)(next->valuedouble - epoch) * 1000 : 0);
    int64_t elapsed = (int64_t)age->valuedouble;
    if (epoch && restored->message_epoch && epoch >= restored->message_epoch)
        elapsed = epoch - restored->message_epoch;
    restored->message_ms = now_ms - elapsed * 1000;
    if ((restored->active && (!restored->name[0] || !restored->question[0]))
        || (restored->waiting && !restored->active) || (restored->alert && !restored->verdict)
        || (restored->message && (!restored->text[0] || !restored->from[0] || !restored->message_id))
        || (restored->verdict && (!restored->verdict_text[0] || !restored->verdict_id)))
        goto invalid;
    restored->alarm_ms = now_ms;
    if (restored->waiting) restored->events |= HOME_EYES_CHANGE;
    home_eyes_expire(restored, epoch);
    *s = *restored;
    free(restored);
    cJSON_Delete(r);
    return true;
invalid:
    free(restored);
    cJSON_Delete(r);
    return false;
#undef LOAD
#undef TEXT
}

static void param(cJSON *object, const char *name, const char *type, const char *description)
{
    cJSON *p = cJSON_AddObjectToObject(object, name);
    cJSON_AddStringToObject(p, "type", type); cJSON_AddStringToObject(p, "description", description);
}

static cJSON *command(cJSON *commands, const char *name, const char *description)
{
    cJSON *c = cJSON_AddObjectToObject(commands, name);
    cJSON_AddStringToObject(c, "description", description);
    cJSON_AddObjectToObject(c, "required");
    cJSON_AddObjectToObject(c, "optional");
    return c;
}

void home_eyes_register(cJSON *commands)
{
    cJSON *c = command(commands, "watch.start", "Watch a fixed scene; notify Muse on change. Replaces one watch. Returns watch state.");
    cJSON *p = cJSON_GetObjectItemCaseSensitive(c, "required");
    param(p, "name", "string", "Place, 1..48 UTF-8 bytes.");
    param(p, "question", "string", "What to inspect, 1..192 UTF-8 bytes.");
    p = cJSON_GetObjectItemCaseSensitive(c, "optional");
    param(p, "interval_min", "integer", "2..120; default 10.");
    param(p, "sensitivity", "string", "low|medium|high; default medium.");
    param(p, "until", "integer", "Future UTC epoch seconds; requires clock.set or SNTP.");
    command(commands, "watch.stop", "Stop the active watch. Leaves cards available for acknowledgement.");
    command(commands, "watch.status", "Read watch, next check, pending verdict, message and alert state.");
    c = command(commands, "watch.verdict", "After a change, report what camera.capture shows. Returns state.");
    p = cJSON_GetObjectItemCaseSensitive(c, "required");
    param(p, "text", "string", "Short factual verdict, 1..480 UTF-8 bytes.");
    param(p, "alert", "boolean", "True for actionable danger; alarm repeats until wheel acknowledgement.");
    c = command(commands, "home.say", "Show a message with a chime; returns immediately. Wheel acknowledges it.");
    p = cJSON_GetObjectItemCaseSensitive(c, "required");
    param(p, "text", "string", "Message, 1..480 UTF-8 bytes.");
    param(p, "from", "string", "Sender, 1..48 UTF-8 bytes.");
}
