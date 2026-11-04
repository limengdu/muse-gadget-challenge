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
#include <string.h>

const char *const air_view_names[AIR_VIEW_COUNT] = {"ambient", "numbers", "night"};
const char *const air_category_names[AIR_CATEGORY_COUNT] = {
    "good", "moderate", "unhealthy_sensitive", "unhealthy", "very_unhealthy", "hazardous"
};
const char *const air_category_labels[AIR_CATEGORY_COUNT] = {
    "Good", "Moderate", "Sensitive groups", "Unhealthy", "Very unhealthy", "Hazardous"
};

void air_core_init(air_core_t *c) {
    memset(c, 0, sizeof(*c));
    c->snapshot.aqi = -1;
    c->snapshot.pm25 = -1;
}

static bool number(const cJSON *p, double lo, double hi, bool integer) {
    return cJSON_IsNumber(p) && isfinite(p->valuedouble)
        && p->valuedouble >= lo && p->valuedouble <= hi
        && (!integer || floor(p->valuedouble) == p->valuedouble);
}

static int name_index(const cJSON *p, const char *const *names, int count) {
    if (!cJSON_IsString(p) || !p->valuestring) return -1;
    for (int i = 0; i < count; i++) if (!strcmp(p->valuestring, names[i])) return i;
    return -1;
}

static bool text(const cJSON *p, char *out, size_t cap) {
    if (!cJSON_IsString(p) || !p->valuestring) return false;
    size_t n = strlen(p->valuestring);
    if (!n || n >= cap) return false;
    bool visible = false;
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)p->valuestring[i];
        if (ch < 32 || ch > 126) return false;
        visible |= ch != ' ';
    }
    if (!visible) return false;
    memcpy(out, p->valuestring, n + 1);
    return true;
}

// Accepts one complete snapshot with unique fields and at least one outdoor metric.
// 接收字段唯一且至少含一项户外指标的完整快照。
bool air_snapshot_parse(air_snapshot_t *out, const cJSON *params) {
    if (!cJSON_IsObject(params)) return false;
    air_snapshot_t next = {.present = true, .aqi = -1, .pm25 = -1};
    const char *const keys[] = {"aqi", "pm25", "category", "advice", "place", "updated_utc"};
    unsigned seen = 0;
    const cJSON *p;
    cJSON_ArrayForEach(p, params) {
        int k = -1;
        for (int i = 0; i < 6; i++) if (p->string && !strcmp(p->string, keys[i])) k = i;
        if (k < 0 || (seen & (1u << k))) return false;
        seen |= 1u << k;
        if (k == 0 || k == 1) {
            if (cJSON_IsNull(p)) continue;
            if (!number(p, 0, k == 0 ? 500 : 1000, k == 0)) return false;
            if (k == 0) next.aqi = (int)p->valuedouble;
            else next.pm25 = p->valuedouble;
        } else if (k == 2) {
            int category = name_index(p, air_category_names, AIR_CATEGORY_COUNT);
            if (category < 0) return false;
            next.category = (air_category_t)category;
        } else if (k == 3) {
            if (!text(p, next.advice, sizeof(next.advice))) return false;
        } else if (k == 4) {
            if (!text(p, next.place, sizeof(next.place))) return false;
        } else {
            if (!number(p, AIR_MIN_EPOCH, AIR_MAX_EPOCH, true)) return false;
            next.updated_utc = (int64_t)p->valuedouble;
        }
    }
    const unsigned required = (1u << 2) | (1u << 3) | (1u << 5);
    if ((seen & required) != required || (next.aqi < 0 && next.pm25 < 0)) return false;
    if (next.aqi >= 0) {
        int category = next.aqi <= 50 ? AIR_GOOD : next.aqi <= 100 ? AIR_MODERATE
            : next.aqi <= 150 ? AIR_SENSITIVE : next.aqi <= 200 ? AIR_UNHEALTHY
            : next.aqi <= 300 ? AIR_VERY_UNHEALTHY : AIR_HAZARDOUS;
        if (category != (int)next.category) return false;
    }
    *out = next;
    return true;
}

int64_t air_snapshot_age(const air_core_t *c, int64_t epoch) {
    if (!c->snapshot.present || epoch < AIR_MIN_EPOCH || epoch > AIR_MAX_EPOCH
        || epoch < c->snapshot.updated_utc) return -1;
    return epoch - c->snapshot.updated_utc;
}

static cJSON *snapshot_json(const air_snapshot_t *s) {
    if (!s->present) return cJSON_CreateNull();
    cJSON *p = cJSON_CreateObject();
    bool ok = p && cJSON_AddItemToObject(p, "aqi", s->aqi >= 0 ? cJSON_CreateNumber(s->aqi) : cJSON_CreateNull())
        && cJSON_AddItemToObject(p, "pm25", s->pm25 >= 0 ? cJSON_CreateNumber(s->pm25) : cJSON_CreateNull())
        && cJSON_AddStringToObject(p, "category", air_category_names[s->category])
        && cJSON_AddStringToObject(p, "advice", s->advice)
        && (!s->place[0] || cJSON_AddStringToObject(p, "place", s->place))
        && cJSON_AddNumberToObject(p, "updated_utc", (double)s->updated_utc);
    if (!ok) { cJSON_Delete(p); return NULL; }
    return p;
}

cJSON *air_core_json(const air_core_t *c) {
    cJSON *p = cJSON_CreateObject(), *snapshot = snapshot_json(&c->snapshot);
    if (!p || !snapshot || !cJSON_AddNumberToObject(p, "version", 1)
        || !cJSON_AddStringToObject(p, "view", air_view_names[c->view])) {
        cJSON_Delete(snapshot); cJSON_Delete(p); return NULL;
    }
    cJSON_AddItemToObject(p, "snapshot", snapshot);
    return p;
}

// Restores a validated versioned record atomically, including its selected view.
// 原子恢复已校验的版本化记录及所选视图。
bool air_core_restore(air_core_t *c, const cJSON *record) {
    if (!cJSON_IsObject(record)) return false;
    air_core_t next;
    air_core_init(&next);
    unsigned seen = 0;
    const cJSON *p;
    cJSON_ArrayForEach(p, record) {
        int k = !strcmp(p->string, "version") ? 0 : !strcmp(p->string, "view") ? 1
            : !strcmp(p->string, "snapshot") ? 2 : -1;
        if (k < 0 || (seen & (1u << k))) return false;
        seen |= 1u << k;
        if (k == 0 && !number(p, 1, 1, true)) return false;
        if (k == 1) {
            int view = name_index(p, air_view_names, AIR_VIEW_COUNT);
            if (view < 0) return false;
            next.view = (air_view_t)view;
        }
        if (k == 2 && !cJSON_IsNull(p) && !air_snapshot_parse(&next.snapshot, p)) return false;
    }
    if (seen != 7) return false;
    *c = next;
    return true;
}

static cJSON *error_result(const char *code, const char *message) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", false);
    cJSON *e = cJSON_AddObjectToObject(r, "error");
    cJSON_AddStringToObject(e, "code", code);
    cJSON_AddStringToObject(e, "message", message);
    return r;
}

// Saves the complete candidate before publishing it as the current snapshot or view.
// 完整保存候选记录后，再发布为当前快照或视图。
cJSON *air_core_command(air_core_t *c, const char *command, const cJSON *params,
                        int64_t epoch, air_save_fn save, void *ctx) {
    if (!command || (params && !cJSON_IsObject(params)))
        return error_result("invalid_params", "params must be an object");
    air_core_t next = *c;
    bool update = !strcmp(command, "air.update"), view = !strcmp(command, "air.set_view");
    if (update) {
        if (!air_snapshot_parse(&next.snapshot, params))
            return error_result("invalid_params", "Provide US AQI or PM2.5, matching category, advice and updated_utc");
        if (c->snapshot.present && next.snapshot.updated_utc < c->snapshot.updated_utc)
            return error_result("older_snapshot", "Keep the most recent outdoor snapshot");
        if (epoch >= AIR_MIN_EPOCH && epoch <= AIR_MAX_EPOCH && next.snapshot.updated_utc > epoch + 300)
            return error_result("future_snapshot", "updated_utc is ahead of the device clock");
    } else if (view) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(params, "view");
        int index = name_index(v, air_view_names, AIR_VIEW_COUNT);
        if (!cJSON_IsObject(params) || cJSON_GetArraySize(params) != 1 || index < 0)
            return error_result("invalid_params", "view must be ambient, numbers or night");
        next.view = (air_view_t)index;
    } else if (!strcmp(command, "air.status")) {
        if (params && params->child) return error_result("invalid_params", "air.status takes no parameters");
    } else return error_result("unsupported_command", "Unknown Outdoor Air command");
    cJSON *payload = cJSON_CreateObject(), *snapshot = snapshot_json(&next.snapshot);
    int64_t age = air_snapshot_age(&next, epoch);
    bool ok = payload && snapshot && cJSON_AddStringToObject(payload, "source", "muse")
        && cJSON_AddStringToObject(payload, "aqi_scale", "US EPA")
        && cJSON_AddStringToObject(payload, "pm25_unit", "ug/m3")
        && cJSON_AddStringToObject(payload, "view", air_view_names[next.view])
        && cJSON_AddItemToObject(payload, "age_s", age >= 0 ? cJSON_CreateNumber((double)age) : cJSON_CreateNull())
        && cJSON_AddBoolToObject(payload, "clock_valid", epoch >= AIR_MIN_EPOCH && epoch <= AIR_MAX_EPOCH)
        && cJSON_AddBoolToObject(payload, "refresh_needed", age < 0 || age >= AIR_STALE_S);
    if (!ok) { cJSON_Delete(payload); cJSON_Delete(snapshot); return error_result("out_of_memory", "Outdoor Air allocation failed"); }
    cJSON_AddItemToObject(payload, "snapshot", snapshot);
    cJSON *result = cJSON_CreateObject();
    if (!result || !cJSON_AddBoolToObject(result, "ok", true)) {
        cJSON_Delete(result); cJSON_Delete(payload); return error_result("out_of_memory", "Outdoor Air allocation failed");
    }
    cJSON_AddItemToObject(result, "payload", payload);
    if ((update || view) && (!save || !save(&next, ctx))) {
        cJSON_Delete(result); return error_result("storage_failed", "Outdoor snapshot and view could not be saved");
    }
    *c = next;
    return result;
}
