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
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static air_core_t c;
static air_sample_t storage[AIR_MINUTES];
static int events;
static bool accepted = true, saved = true;
static char last_summary[192];
static cJSON *last_data;
static bool post(const char *summary, const cJSON *data, void *ctx) {
    (void)ctx;
    if (!accepted) return false;
    events++;
    snprintf(last_summary, sizeof(last_summary), "%s", summary);
    cJSON_Delete(last_data); last_data = cJSON_Duplicate(data, true);
    return true;
}
static bool save(const air_settings_t *s, void *ctx) { (void)s; (void)ctx; return saved; }
static void init(void) {
    air_core_init(&c, storage); events = 0; accepted = saved = true;
}
static void observe(float co2, float tvoc, float temp, float humidity, int mask, int64_t t) {
    float v[] = {co2, tvoc, temp, humidity};
    air_core_observe(&c, v, (uint8_t)mask, t);
}
static void evaluate(int64_t t) { air_core_evaluate(&c, t, 1760000000 + t, post, NULL); }
static cJSON *command(const char *name, const char *json, bool success) {
    cJSON *p = json ? cJSON_Parse(json) : NULL;
    cJSON *r = air_core_command(&c, name, p, 604800, 1760000000, 700, save, NULL);
    assert(r && cJSON_IsTrue(cJSON_GetObjectItem(r, "ok")) == success);
    cJSON_Delete(p); return r;
}
static void history(void) {
    init();
    for (int i = 0; i < AIR_MINUTES + 100; i++) {
        observe(500 + i % 100, 100, 23, 45, i % 3 ? 15 : 1, i * 60LL);
        observe(9999, 500, 80, 99, 15, i * 60LL + 30);
    }
    assert(c.count == AIR_MINUTES);
    assert(c.history[c.head].time_s == 6000);
    air_buckets_t b;
    int64_t now = (AIR_MINUTES + 99) * 60LL;
    air_core_buckets(&c, 168, 1, now, &b);
    assert(b.count == 24 && b.step_min == 420);
    unsigned total = 0, humid = 0;
    for (unsigned i = 0; i < b.count; i++) {
        air_bucket_t *a = &b.metrics[0][i];
        total += a->count; humid += b.metrics[3][i].count;
        assert(a->min >= 500 && a->max <= 599 && a->avg >= a->min && a->avg <= a->max);
    }
    assert(total == AIR_MINUTES && humid < total);
    air_core_buckets(&c, 1, 10080, now, &b);
    assert(b.count == 1 && b.metrics[0][0].count == 60);
    init();
    observe(400, 100, 20, 40, 15, 0);
    observe(600, 100, 20, 40, 15, 60);
    observe(800, 100, 20, 40, 15, 120);
    air_core_buckets(&c, 1, 60, 120, &b);
    assert(b.metrics[0][0].min == 400 && b.metrics[0][0].avg == 600 && b.metrics[0][0].max == 800);
    air_core_buckets(&c, 1, 1, 120, &b);
    assert(b.count == 20 && b.step_min == 3 && b.metrics[0][0].count == 0);
    observe(NAN, INFINITY, -99, 200, 15, 180);
    assert(c.valid == 0 && !c.history[3].valid);
}
static void thresholds(void) {
    init();
    observe(1000, 100, 23, 45, 15, 0); evaluate(0); assert(events == 0);
    observe(1520, 100, 23, 45, 15, 1); evaluate(1);
    assert(events == 1 && strstr(last_summary, "Living room") && c.alarms[0].active);
    evaluate(1802); assert(events == 1);
    observe(950, 100, 23, 45, 15, 1803); evaluate(1803); assert(events == 1);
    observe(860, 100, 23, 45, 15, 1804); evaluate(1804);
    assert(events == 2 && !c.alarms[0].active && strstr(last_summary, "back to normal"));
    observe(1520, 100, 23, 45, 15, 1805); evaluate(1805); assert(events == 3);
    observe(800, 100, 23, 45, 15, 1806); evaluate(1806); assert(events == 4);
    observe(1520, 100, 23, 45, 15, 1807); evaluate(1807); assert(events == 4 && c.alarms[0].active);
    observe(800, 100, 23, 45, 15, 1808); evaluate(1808); assert(events == 4);
    observe(1520, 100, 23, 45, 15, 1809); evaluate(1809); assert(events == 4);
    evaluate(3605); assert(events == 5);
    observe(0, 0, 0, 0, 0, 3606); evaluate(3606); assert(events == 5 && c.alarms[0].active);
    observe(800, 100, 23, 45, 15, 3607); evaluate(3607); assert(events == 6);
    init(); accepted = false;
    observe(1520, 100, 23, 45, 15, 0); evaluate(0);
    assert(!c.alarms[0].announced && events == 0);
    accepted = true; evaluate(1); assert(events == 1);
    observe(800, 100, 23, 45, 15, 2); accepted = false; evaluate(2);
    assert(c.alarms[0].announced); accepted = true; evaluate(3); assert(events == 2);
}
static void all_thresholds(void) {
    for (int i = 0; i < AIR_LIMITS; i++) {
        init(); float v[] = {600, 100, 23, 45};
        int m = i < 2 ? i : i < 4 ? 2 : 3;
        v[m] = c.settings.limits[i] + ((i == 3 || i == 5) ? -2 : 2);
        air_core_observe(&c, v, 15, 0); evaluate(0);
        assert(events == 1 && c.alarms[i].active);
        v[m] = m == 0 ? 600 : m == 1 ? 100 : m == 2 ? 23 : 45;
        air_core_observe(&c, v, 15, 1); evaluate(1);
        assert(events == 2 && !c.alarms[i].active);
        assert(!strcmp(cJSON_GetObjectItem(last_data, "kind")->valuestring, "recovery"));
    }
}
static void alternating_limits(void) {
    init();
    observe(600, 100, 30, 45, 15, 0); evaluate(0); assert(events == 1);
    observe(600, 100, 23, 45, 15, 1); evaluate(1); assert(events == 2);
    observe(600, 100, 12, 45, 15, 2); evaluate(2); assert(events == 2 && c.alarms[3].active);
    evaluate(1800); assert(events == 3 && c.alarms[3].announced);
}
static void views(void) {
    assert(air_effective_view(AIR_AUTO, -1) == AIR_AMBIENT);
    assert(air_effective_view(AIR_AUTO, 0) == AIR_NIGHT);
    assert(air_effective_view(AIR_AUTO, 419) == AIR_NIGHT);
    assert(air_effective_view(AIR_AUTO, 420) == AIR_AMBIENT);
    assert(air_effective_view(AIR_AUTO, 1319) == AIR_AMBIENT);
    assert(air_effective_view(AIR_AUTO, 1320) == AIR_NIGHT);
    for (int i = 0; i < 3; i++) assert(air_effective_view((air_view_t)i, 0) == (air_view_t)i);
}
static void validation(void) {
    init();
    const char *bad[] = {"{}", "[]", "{\"other\":1}", "{\"co2_ppm\":399}", "{\"co2_ppm\":10001}",
        "{\"tvoc\":501}", "{\"co2_ppm\":1000.5}", "{\"co2_ppm\":null}", "{\"tvoc\":\"250\"}",
        "{\"room\":\"\"}", "{\"room\":\"   \"}", "{\"room\":\"A\\nB\"}", "{\"room\":\"\\u4e2d\"}",
        "{\"room\":\"01234567890123456789012345678901234567890\"}",
        "{\"temp_high_c\":15}", "{\"temp_low_c\":30}", "{\"humidity_high\":31}", "{\"humidity_low\":60}",
        "{\"co2_ppm\":500,\"co2_ppm\":600}", "{\"view\":\"auto\"}"};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) cJSON_Delete(command("air.set_thresholds", bad[i], false));
    cJSON_Delete(command("air.set_thresholds", "{\"co2_ppm\":1500,\"room\":\"Bedroom\",\"temp_high_c\":27.5}", true));
    assert(c.settings.limits[0] == 1500 && c.settings.limits[2] == 27.5f && !strcmp(c.settings.room, "Bedroom"));
    saved = false; cJSON_Delete(command("air.set_thresholds", "{\"co2_ppm\":1800}", false));
    assert(c.settings.limits[0] == 1500); saved = true;
    const char *views[] = {"ambient", "numbers", "night", "auto"};
    for (int i = 0; i < 4; i++) {
        char p[64]; snprintf(p, sizeof(p), "{\"view\":\"%s\"}", views[i]);
        cJSON_Delete(command("air.set_view", p, true)); assert(c.settings.view == (air_view_t)i);
    }
    const char *badview[] = {NULL, "{}", "[]", "{\"view\":true}", "{\"view\":\"other\"}", "{\"view\":\"night\",\"room\":\"A\"}"};
    for (unsigned i = 0; i < sizeof(badview)/sizeof(badview[0]); i++) cJSON_Delete(command("air.set_view", badview[i], false));
    cJSON_Delete(command("air.status", NULL, true)); cJSON_Delete(command("air.status", "{\"foo\":1}", false));
    cJSON_Delete(command("air.status", "[]", false));
    const char *badhistory[] = {"[]", "{\"hours\":0}", "{\"hours\":169}", "{\"hours\":1.5}", "{\"hours\":true}",
        "{\"step_min\":0}", "{\"step_min\":10081}", "{\"step_min\":1.5}", "{\"other\":2}", "{\"hours\":1,\"hours\":2}"};
    for (unsigned i = 0; i < sizeof(badhistory)/sizeof(badhistory[0]); i++) cJSON_Delete(command("sensors.history", badhistory[i], false));
    cJSON_Delete(command("sensors.history", NULL, true));
    cJSON_Delete(command("other", NULL, false));
    cJSON *p = cJSON_CreateObject(); cJSON_AddNumberToObject(p, "temp_high_c", 0);
    cJSON_GetObjectItem(p, "temp_high_c")->valuedouble = NAN;
    air_settings_t out; assert(!air_settings_parse(&out, &c.settings, p, false)); cJSON_Delete(p);
}
static void response_size(void) {
    init();
    for (int i = 0; i < AIR_MINUTES; i++) observe(39999.9f, 499.9f, -39.9f, 99.9f, 15, i * 60LL);
    cJSON *r = command("sensors.history", "{\"hours\":168,\"step_min\":1}", true);
    char *s = cJSON_PrintUnformatted(r); assert(s && strlen(s) < 8000);
    cJSON *p = cJSON_GetObjectItem(r, "payload");
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(p, "timestamps")) == 24);
    printf("history: %zu bytes\n", strlen(s)); cJSON_free(s); cJSON_Delete(r);
    r = air_core_command(&c, "sensors.history", NULL, 604800, 0, -1, save, NULL);
    p = cJSON_GetObjectItem(r, "payload");
    assert(!strcmp(cJSON_GetObjectItem(p, "time_basis")->valuestring, "uptime")); cJSON_Delete(r);
}
static void previews(void) {
    init();
    for (int i = 0; i < 1440; i++) observe(600 + 130 * sinf(i / 100.0f), 96, 23.4f, 46, 15, i * 60);
    const char *dir = getenv("CARD_PREVIEW_DIR");
    if (dir) mkdir(dir, 0755);
    uint16_t *pixels = malloc(480 * 480 * 2);
    card_rgb565_t canvas;
    assert(!card_rgb565_canvas(&canvas, pixels, 1, 480, 480));
    assert(card_rgb565_canvas(&canvas, pixels, 480 * 480 * 2, 480, 480));
    const char *names[] = {"ambient", "numbers", "night", "alert"};
    for (int i = 0; i < 4; i++) {
        c.settings.view = i == 3 ? AIR_AMBIENT : (air_view_t)i;
        if (i == 3) { observe(1520, 100, 23, 45, 15, 86400); evaluate(86400); }
        air_guardian_render(&canvas, &c, 86400, 23 * 60, true);
        assert(pixels[0] == (i == 3 ? card_rgb565_color(245,101,93)
            : i == 2 ? card_rgb565_color(3,3,4) : card_rgb565_color(13,25,30)));
        if (!dir) continue;
        char path[4096]; snprintf(path, sizeof(path), "%s/air_%s.ppm", dir, names[i]);
        FILE *f = fopen(path, "wb"); assert(f); fputs("P6\n480 480\n255\n", f);
        for (int n = 0; n < 480 * 480; n++) {
            uint16_t p = pixels[n];
            unsigned char rgb[] = {(unsigned char)(((p >> 11) & 31) * 255 / 31),
                (unsigned char)(((p >> 5) & 63) * 255 / 63), (unsigned char)((p & 31) * 255 / 31)};
            assert(fwrite(rgb, 1, 3, f) == 3);
        }
        fclose(f);
    }
    card_rgb565_rect(&canvas, -10, -10, 20, 20, 0);
    card_rgb565_text(&canvas, -20, -20, 20, "A\xc2\xb0", 96, 0);
    assert(card_rgb565_icon(&canvas, 460, 460, "air", 112, 0));
    assert(!card_rgb565_icon(&canvas, 0, 0, "missing", 64, 0));
    free(pixels);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "history")) history();
    else if (!strcmp(argv[1], "thresholds")) thresholds();
    else if (!strcmp(argv[1], "all_thresholds")) all_thresholds();
    else if (!strcmp(argv[1], "alternating")) alternating_limits();
    else if (!strcmp(argv[1], "views")) views();
    else if (!strcmp(argv[1], "validation")) validation();
    else if (!strcmp(argv[1], "size")) response_size();
    else if (!strcmp(argv[1], "previews")) previews();
    else assert(false);
    cJSON_Delete(last_data); puts("ok"); return 0;
}
