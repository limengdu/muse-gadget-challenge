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

#include "air_guardian_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const air_metric_names[AIR_METRICS] = {"co2", "tvoc", "temperature", "humidity"};
const char *const air_limit_names[AIR_LIMITS] = {
    "co2_ppm", "tvoc", "temp_high_c", "temp_low_c", "humidity_high", "humidity_low"
};
const char *const air_view_names[4] = {"ambient", "numbers", "night", "auto"};
static const int metric_for_limit[AIR_LIMITS] = {0, 1, 2, 2, 3, 3};
static const float hysteresis[AIR_LIMITS] = {100, 25, 1, 1, 5, 5};
static const float lower[AIR_LIMITS] = {400, 1, -40, -40, 0, 0};
static const float upper[AIR_LIMITS] = {10000, 500, 85, 85, 100, 100};
static const char *const metric_labels[AIR_METRICS] = {"CO2", "tVOC", "temperature", "humidity"};
static const char *const units[AIR_METRICS] = {"ppm", "index", "C", "% RH"};
static const char *const advice[AIR_LIMITS] = {
    "Opening a window helps.", "Ventilate; check sources.",
    "Cool room; check occupants.", "Warm room; check occupants.",
    "Ventilate; reduce moisture.", "Check the room's humidity."
};

void air_core_init(air_core_t *c, air_sample_t *storage) {
    memset(c, 0, sizeof(*c));
    c->history = storage;
    c->settings = (air_settings_t){{1000, 250, 28, 16, 65, 30}, "Living room", AIR_AMBIENT};
}

// Keeps fresh, physically bounded values and records at most one sample per minute.
// 保留新鲜且在物理范围内的读数，每分钟最多记录一次。
void air_core_observe(air_core_t *c, const float values[AIR_METRICS], uint8_t valid, int64_t now_s) {
    static const float lo[AIR_METRICS] = {0, 1, -40, 0};
    static const float hi[AIR_METRICS] = {40000, 500, 85, 100};
    c->valid = valid & 15;
    for (int i = 0; i < AIR_METRICS; i++) {
        if (!isfinite(values[i]) || values[i] < lo[i] || values[i] > hi[i]) c->valid &= ~(1u << i);
        c->current[i] = (c->valid & (1u << i)) ? values[i] : 0;
    }
    if (!c->history || (c->sampled && now_s - c->sampled_s < 60)) return;
    air_sample_t *s = &c->history[c->head];
    *s = (air_sample_t){.time_s = now_s, .valid = c->valid};
    memcpy(s->values, c->current, sizeof(s->values));
    c->head = (c->head + 1) % AIR_MINUTES;
    if (c->count < AIR_MINUTES) c->count++;
    c->sampled = true;
    c->sampled_s = now_s;
}

static bool low_limit(int i) { return i == 3 || i == 5; }

// Sends one crossing and its matching recovery; failed queue attempts remain pending.
// 发送一次越界及对应的恢复消息，入队失败的消息保持待发送状态。
void air_core_evaluate(air_core_t *c, int64_t now_s, int64_t epoch, air_post_fn post, void *ctx) {
    for (int i = 0; i < AIR_LIMITS; i++) {
        int m = metric_for_limit[i];
        if (!(c->valid & (1u << m))) continue;
        air_alarm_t *a = &c->alarms[i];
        float v = c->current[m], limit = c->settings.limits[i];
        bool low = low_limit(i);
        bool crossing = low ? v < limit : v > limit;
        bool recovered = low ? v >= limit + hysteresis[i] : v <= limit - hysteresis[i];
        if (!a->active && crossing) a->active = true;
        if (a->active && recovered) a->active = false;
        bool cooled_down = true;
        for (int j = 0; j < AIR_LIMITS; j++) {
            const air_alarm_t *other = &c->alarms[j];
            if (metric_for_limit[j] == m && other->sent_before
                && now_s - other->last_sent_s < AIR_COOLDOWN_S) cooled_down = false;
        }
        bool alert = a->active && !a->announced && cooled_down;
        bool recovery = !a->active && a->announced;
        if (!alert && !recovery) continue;
        char summary[192];
        if (alert) {
            snprintf(summary, sizeof(summary), "%s %s is %.1f %s (%s limit %.1f). %s",
                     c->settings.room, metric_labels[m], (double)v, units[m],
                     low ? "low" : "high", (double)limit, advice[i]);
        } else {
            snprintf(summary, sizeof(summary), "%s is back to normal: %s %.1f %s.",
                     c->settings.room, metric_labels[m], (double)v, units[m]);
        }
        cJSON *data = cJSON_CreateObject();
        if (!data) continue;
        cJSON_AddStringToObject(data, "kind", alert ? "alert" : "recovery");
        cJSON_AddStringToObject(data, "metric", air_metric_names[m]);
        cJSON_AddStringToObject(data, "threshold", air_limit_names[i]);
        cJSON_AddStringToObject(data, "room", c->settings.room);
        cJSON_AddNumberToObject(data, "value", v);
        cJSON_AddNumberToObject(data, "limit", limit);
        if (post && post(summary, data, ctx)) {
            a->announced = alert;
            if (alert) { a->last_sent_s = now_s; a->sent_before = true; }
            a->last_event_time = epoch > 0 ? epoch : now_s;
            a->last_event_epoch = epoch > 0;
            snprintf(a->last_message, sizeof(a->last_message), "%s", summary);
        }
        cJSON_Delete(data);
    }
}

// Buckets monotonic minute samples into a bounded window, retaining explicit gaps.
// 将单调时钟记录的分钟样本合并到有界窗口，并保留明确的缺失区间。
void air_core_buckets(const air_core_t *c, int hours, int step_min, int64_t now_s, air_buckets_t *out) {
    memset(out, 0, sizeof(*out));
    if (hours < 1 || hours > 168 || step_min < 1 || step_min > AIR_MINUTES) return;
    int minimum_step = (hours * 60 + AIR_POINTS - 1) / AIR_POINTS;
    out->step_min = step_min > minimum_step ? (unsigned)step_min : (unsigned)minimum_step;
    out->count = (hours * 60 + out->step_min - 1) / out->step_min;
    int64_t start = now_s - hours * 3600LL + 1;
    for (unsigned b = 0; b < out->count; b++) out->times[b] = start + b * out->step_min * 60LL;
    for (unsigned n = 0; n < c->count; n++) {
        const air_sample_t *s = &c->history[(c->head + AIR_MINUTES - c->count + n) % AIR_MINUTES];
        if (s->time_s < start || s->time_s > now_s) continue;
        unsigned index = (unsigned)((s->time_s - start) / (out->step_min * 60LL));
        if (index >= out->count) continue;
        for (int m = 0; m < AIR_METRICS; m++) {
            if (!(s->valid & (1u << m))) continue;
            air_bucket_t *b = &out->metrics[m][index];
            float value = s->values[m];
            if (!b->count || value < b->min) b->min = value;
            if (!b->count || value > b->max) b->max = value;
            b->avg += value;
            b->count++;
        }
    }
    for (int m = 0; m < AIR_METRICS; m++)
        for (unsigned n = 0; n < out->count; n++) {
            air_bucket_t *b = &out->metrics[m][n];
            if (b->count) b->avg /= b->count;
        }
}

air_view_t air_effective_view(air_view_t view, int minutes) {
    return view == AIR_AUTO ? (minutes >= 22 * 60 || (minutes >= 0 && minutes < 7 * 60)
        ? AIR_NIGHT : AIR_AMBIENT) : view;
}

int air_core_level(const air_core_t *c, int m) {
    if (m < 0 || m >= AIR_METRICS || !(c->valid & (1u << m))) return -1;
    for (int i = 0; i < AIR_LIMITS; i++) if (metric_for_limit[i] == m && c->alarms[i].active) return 3;
    float v = c->current[m];
    if (m < 2) {
        float limit = c->settings.limits[m];
        if (v > limit) return 3;
        if (v >= limit * .9f) return 2;
        if (v >= limit * .75f) return 1;
    } else {
        int hi = m == 2 ? 2 : 4, lo = hi + 1;
        if (v > c->settings.limits[hi] || v < c->settings.limits[lo]) return 3;
        if (v > c->settings.limits[hi] - hysteresis[hi]
            || v < c->settings.limits[lo] + hysteresis[lo]) return 1;
    }
    return 0;
}

static int first_active(const air_core_t *c) {
    for (int i = 0; i < AIR_LIMITS; i++) if (c->alarms[i].active) return i;
    return -1;
}

const char *air_core_mood(const air_core_t *c) {
    if (!c->valid) return "Waiting for sensors";
    if (c->alarms[2].active) return "Too warm";
    if (c->alarms[3].active) return "Too cool";
    if (c->alarms[0].active || c->alarms[1].active) return "Open a window";
    if (c->alarms[4].active || c->alarms[5].active) return "Check humidity";
    if (c->valid != 15) return "Sensors missing";
    if (air_core_level(c, 0) > 0 || air_core_level(c, 1) > 0) return "Getting stuffy";
    return "Fresh";
}

const char *air_core_advice(const air_core_t *c) {
    int i = first_active(c);
    if (i >= 0 && !(c->valid & (1u << metric_for_limit[i]))) return "Check the sensor connection.";
    return i >= 0 ? advice[i] : c->valid != 15 ? "Check the sensor connection." : "Air Guardian";
}

static bool object_params(const cJSON *params) { return !params || cJSON_IsObject(params); }

bool air_settings_parse(air_settings_t *out, const air_settings_t *base, const cJSON *params, bool view) {
    if (!cJSON_IsObject(params) || !params->child) return false;
    *out = *base;
    unsigned seen = 0;
    const cJSON *p;
    cJSON_ArrayForEach(p, params) {
        if (!p->string) return false;
        int key = -1;
        for (int i = 0; i < AIR_LIMITS; i++) if (!strcmp(p->string, air_limit_names[i])) key = i;
        if (!strcmp(p->string, "room")) key = AIR_LIMITS;
        if (!strcmp(p->string, "view")) key = AIR_LIMITS + 1;
        if (key < 0 || (view ? key != AIR_LIMITS + 1 : key == AIR_LIMITS + 1) || (seen & (1u << key))) return false;
        seen |= 1u << key;
        if (key < AIR_LIMITS) {
            if (!cJSON_IsNumber(p) || !isfinite(p->valuedouble)
                || p->valuedouble < lower[key] || p->valuedouble > upper[key]) return false;
            if (key < 2 && floor(p->valuedouble) != p->valuedouble) return false;
            out->limits[key] = (float)p->valuedouble;
        } else if (key == AIR_LIMITS) {
            if (!cJSON_IsString(p) || !p->valuestring || !p->valuestring[0]
                || strlen(p->valuestring) >= AIR_ROOM_BYTES) return false;
            bool visible = false;
            for (const unsigned char *s = (const unsigned char *)p->valuestring; *s; s++) {
                if (*s < 32 || *s > 126) return false;
                visible |= *s != ' ';
            }
            if (!visible) return false;
            strcpy(out->room, p->valuestring);
        } else {
            if (!cJSON_IsString(p) || !p->valuestring) return false;
            int i;
            for (i = 0; i < 4; i++) if (!strcmp(p->valuestring, air_view_names[i])) break;
            if (i == 4) return false;
            out->view = (air_view_t)i;
        }
    }
    return out->limits[2] >= out->limits[3] + 2 && out->limits[4] >= out->limits[5] + 10;
}

cJSON *air_settings_json(const air_settings_t *s) {
    cJSON *p = cJSON_CreateObject();
    if (!p) return NULL;
    for (int i = 0; i < AIR_LIMITS; i++) {
        if (!cJSON_AddNumberToObject(p, air_limit_names[i], s->limits[i])) {
            cJSON_Delete(p);
            return NULL;
        }
    }
    if (!cJSON_AddStringToObject(p, "room", s->room)) {
        cJSON_Delete(p);
        return NULL;
    }
    return p;
}

static cJSON *error_result(const char *code, const char *message) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", false);
    cJSON *e = cJSON_AddObjectToObject(r, "error");
    cJSON_AddStringToObject(e, "code", code);
    cJSON_AddStringToObject(e, "message", message);
    return r;
}

static double decimal(float v) { return round((double)v * 10) / 10; }

static cJSON *status_payload(const air_core_t *c, int minutes) {
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "room", c->settings.room);
    cJSON_AddStringToObject(p, "view", air_view_names[c->settings.view]);
    cJSON_AddStringToObject(p, "effective_view", air_view_names[air_effective_view(c->settings.view, minutes)]);
    cJSON_AddBoolToObject(p, "local_time_valid", minutes >= 0);
    cJSON_AddStringToObject(p, "mood", air_core_mood(c));
    cJSON_AddItemToObject(p, "thresholds", air_settings_json(&c->settings));
    cJSON *values = cJSON_AddObjectToObject(p, "values");
    cJSON *levels = cJSON_AddObjectToObject(p, "levels");
    static const char *const names[] = {"fresh", "watch", "stuffy", "alert"};
    for (int m = 0; m < AIR_METRICS; m++) {
        int level = air_core_level(c, m);
        if (level < 0) cJSON_AddNullToObject(values, air_metric_names[m]);
        else {
            cJSON *reading = cJSON_AddObjectToObject(values, air_metric_names[m]);
            cJSON_AddNumberToObject(reading, "value", decimal(c->current[m]));
            cJSON_AddStringToObject(reading, "unit", units[m]);
        }
        cJSON_AddStringToObject(levels, air_metric_names[m], level < 0 ? "missing" : names[level]);
    }
    cJSON *alerts = cJSON_AddArrayToObject(p, "last_alerts");
    for (int i = 0; i < AIR_LIMITS; i++) {
        const air_alarm_t *a = &c->alarms[i];
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "threshold", air_limit_names[i]);
        cJSON_AddBoolToObject(e, "active", a->active);
        if (a->last_message[0]) {
            cJSON_AddStringToObject(e, "summary", a->last_message);
            cJSON_AddNumberToObject(e, "time", (double)a->last_event_time);
            cJSON_AddStringToObject(e, "time_basis", a->last_event_epoch ? "epoch" : "uptime");
        }
        cJSON_AddItemToArray(alerts, e);
    }
    return p;
}

// Returns compact aligned arrays; each tuple is minimum, average, maximum and sample count.
// 返回紧凑的对齐数组，每个元组依次为最小值、平均值、最大值与样本数。
static cJSON *history_payload(const air_core_t *c, int hours, int step, int64_t now_s, int64_t epoch) {
    air_buckets_t *b = malloc(sizeof(*b));
    if (!b) return NULL;
    air_core_buckets(c, hours, step, now_s, b);
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "time_basis", epoch > 0 ? "epoch" : "uptime");
    cJSON_AddNumberToObject(p, "hours", hours);
    cJSON_AddNumberToObject(p, "step_min", b->step_min);
    cJSON_AddStringToObject(p, "tuple", "min,avg,max,count");
    cJSON *times = cJSON_AddArrayToObject(p, "timestamps");
    for (unsigned n = 0; n < b->count; n++)
        cJSON_AddItemToArray(times, cJSON_CreateNumber((double)(b->times[n] + (epoch > 0 ? epoch - now_s : 0))));
    for (int m = 0; m < AIR_METRICS; m++) {
        cJSON *array = cJSON_AddArrayToObject(p, air_metric_names[m]);
        for (unsigned n = 0; n < b->count; n++) {
            air_bucket_t *v = &b->metrics[m][n];
            if (!v->count) { cJSON_AddItemToArray(array, cJSON_CreateNull()); continue; }
            cJSON *tuple = cJSON_CreateArray();
            cJSON_AddItemToArray(tuple, cJSON_CreateNumber(decimal(v->min)));
            cJSON_AddItemToArray(tuple, cJSON_CreateNumber(decimal(v->avg)));
            cJSON_AddItemToArray(tuple, cJSON_CreateNumber(decimal(v->max)));
            cJSON_AddItemToArray(tuple, cJSON_CreateNumber(v->count));
            cJSON_AddItemToArray(array, tuple);
        }
    }
    free(b);
    return p;
}

cJSON *air_core_command(air_core_t *c, const char *command, const cJSON *params,
                        int64_t now_s, int64_t epoch, int minutes, air_save_fn save, void *ctx) {
    if (!object_params(params)) return error_result("invalid_params", "params must be an object");
    cJSON *payload = NULL;
    if (!strcmp(command, "air.set_thresholds") || !strcmp(command, "air.set_view")) {
        air_settings_t next_settings;
        bool view = !strcmp(command, "air.set_view");
        if (!air_settings_parse(&next_settings, &c->settings, params, view))
            return error_result("invalid_params", "Check parameter names, types, ranges and high/low limits");
        if (!save || !save(&next_settings, ctx)) return error_result("storage_failed", "Settings could not be saved");
        c->settings = next_settings;
        payload = status_payload(c, minutes);
    } else if (!strcmp(command, "air.status")) {
        if (params && params->child) return error_result("invalid_params", "air.status takes no parameters");
        payload = status_payload(c, minutes);
    } else if (!strcmp(command, "sensors.history")) {
        int hours = 24, step = 1;
        unsigned seen = 0;
        const cJSON *p;
        cJSON_ArrayForEach(p, params) {
            int k = !strcmp(p->string, "hours") ? 0 : !strcmp(p->string, "step_min") ? 1 : -1;
            if (k < 0 || (seen & (1u << k)) || !cJSON_IsNumber(p) || !isfinite(p->valuedouble)
                || p->valuedouble < 1 || p->valuedouble > (k ? AIR_MINUTES : 168)
                || floor(p->valuedouble) != p->valuedouble)
                return error_result("invalid_params", "hours must be 1..168 and step_min 1..10080, both integers");
            seen |= 1u << k;
            if (!k) hours = (int)p->valuedouble; else step = (int)p->valuedouble;
        }
        payload = history_payload(c, hours, step, now_s, epoch);
    } else return error_result("unsupported_command", "Unknown Air Guardian command");
    if (!payload) return error_result("out_of_memory", "Air Guardian allocation failed");
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddItemToObject(result, "payload", payload);
    return result;
}
