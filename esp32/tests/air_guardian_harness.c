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

static air_core_t c, saved;
static bool reject_save;
static unsigned saves;
static const int64_t NOW = 1793793600;
static const char *GOOD = "{\"aqi\":42,\"pm25\":7.1,\"category\":\"good\",\"advice\":\"Enjoy your usual plans.\",\"place\":\"San Francisco\",\"updated_utc\":1793793600}";

static bool save(const air_core_t *next, void *ctx) {
    (void)ctx; saves++;
    if (reject_save) return false;
    saved = *next; return true;
}
static cJSON *command(const char *name, const char *json, int64_t utc, bool ok) {
    cJSON *params = json ? cJSON_Parse(json) : NULL;
    cJSON *r = air_core_command(&c, name, params, utc, save, NULL);
    assert(r && cJSON_IsTrue(cJSON_GetObjectItem(r, "ok")) == ok);
    cJSON_Delete(params); return r;
}
static void run(const char *name, const char *json, bool ok) { cJSON_Delete(command(name, json, NOW, ok)); }
static void init(void) { air_core_init(&c); saves = 0; reject_save = false; }
static void replacement(const char *key, const char *value, bool ok) {
    cJSON *p = cJSON_Parse(GOOD);
    cJSON_ReplaceItemInObject(p, key, cJSON_Parse(value));
    char *json = cJSON_PrintUnformatted(p); run("air.update", json, ok);
    cJSON_free(json); cJSON_Delete(p);
}
static void validation(void) {
    init(); run("air.update", GOOD, true); air_core_t before = c;
    const char *bad[] = {NULL, "{}", "[]", "true", "{\"aqi\":42}",
        "{\"aqi\":42,\"aqi\":42,\"category\":\"good\",\"advice\":\"Fine\",\"updated_utc\":1793793600}",
        "{\"aqi\":null,\"pm25\":null,\"category\":\"good\",\"advice\":\"Fine\",\"updated_utc\":1793793600}",
        "{\"aqi\":42,\"category\":\"good\",\"advice\":\"Fine\",\"updated_utc\":1793793600,\"extra\":1}"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) run("air.update", bad[i], false);
    const char *bad_aqi[] = {"-1", "501", "42.1", "\"42\"", "true", "{}"};
    for (unsigned i = 0; i < sizeof(bad_aqi) / sizeof(bad_aqi[0]); i++) replacement("aqi", bad_aqi[i], false);
    replacement("pm25", "-0.1", false); replacement("pm25", "1001", false);
    replacement("category", "\"moderate\"", false); replacement("category", "\"unknown\"", false);
    replacement("category", "1", false); replacement("advice", "\"\"", false);
    replacement("advice", "\"   \"", false); replacement("advice", "\"a\\nb\"", false);
    replacement("advice", "\"caf\\u00e9\"", false); replacement("place", "null", false);
    replacement("updated_utc", "0", false); replacement("updated_utc", "4102444800", false);
    replacement("updated_utc", "1793793600.1", false); replacement("updated_utc", "1793793901", false);
    replacement("updated_utc", "1793793599", false);
    assert(!memcmp(&c, &before, sizeof(c)) && saves == 1);
    cJSON *p = cJSON_Parse(GOOD); air_snapshot_t s;
    cJSON_GetObjectItem(p, "pm25")->valuedouble = NAN; assert(!air_snapshot_parse(&s, p));
    cJSON_GetObjectItem(p, "pm25")->valuedouble = INFINITY; assert(!air_snapshot_parse(&s, p));
    cJSON_SetNumberValue(cJSON_GetObjectItem(p, "pm25"), 0);
    char long_text[82]; memset(long_text, 'a', 81); long_text[81] = 0;
    cJSON_ReplaceItemInObject(p, "advice", cJSON_CreateString(long_text)); assert(!air_snapshot_parse(&s, p));
    long_text[80] = 0; cJSON_ReplaceItemInObject(p, "advice", cJSON_CreateString(long_text)); assert(air_snapshot_parse(&s, p));
    long_text[41] = 0; cJSON_ReplaceItemInObject(p, "place", cJSON_CreateString(long_text)); assert(!air_snapshot_parse(&s, p));
    long_text[40] = 0; cJSON_ReplaceItemInObject(p, "place", cJSON_CreateString(long_text)); assert(air_snapshot_parse(&s, p));
    cJSON_Delete(p);
}
static void boundaries(void) {
    init(); const int values[] = {0, 50, 51, 100, 101, 150, 151, 200, 201, 300, 301, 500};
    for (int i = 0; i < 12; i++) {
        char json[256]; snprintf(json, sizeof(json),
            "{\"aqi\":%d,\"category\":\"%s\",\"advice\":\"Source advice\",\"updated_utc\":%lld}",
            values[i], air_category_names[i / 2], (long long)NOW);
        run("air.update", json, true);
        assert((int)c.snapshot.category == i / 2 && c.snapshot.pm25 == -1 && !c.snapshot.place[0]);
    }
    run("air.update", "{\"pm25\":0,\"category\":\"good\",\"advice\":\"Fine\",\"updated_utc\":1793793600}", true);
    assert(c.snapshot.aqi == -1 && c.snapshot.pm25 == 0);
    run("air.update", "{\"aqi\":null,\"pm25\":1000,\"category\":\"hazardous\",\"advice\":\"Source advice\",\"updated_utc\":1793793600}", true);
    assert(c.snapshot.aqi == -1 && c.snapshot.pm25 == 1000);
    cJSON *p = cJSON_Parse(GOOD);
    cJSON_SetNumberValue(cJSON_GetObjectItem(p, "updated_utc"), AIR_MIN_EPOCH); assert(air_snapshot_parse(&c.snapshot, p));
    cJSON_SetNumberValue(cJSON_GetObjectItem(p, "updated_utc"), AIR_MAX_EPOCH); assert(air_snapshot_parse(&c.snapshot, p));
    cJSON_Delete(p);
}
static void persistence(void) {
    init(); run("air.update", GOOD, true); run("air.set_view", "{\"view\":\"night\"}", true);
    cJSON *p = air_core_json(&c); char *json = cJSON_PrintUnformatted(p); assert(json && strlen(json) < 1024);
    cJSON_Delete(p); p = cJSON_Parse(json); cJSON_free(json);
    air_core_t restored; air_core_init(&restored); assert(air_core_restore(&restored, p));
    assert(restored.view == AIR_NIGHT && restored.snapshot.aqi == 42 && restored.snapshot.pm25 == 7.1);
    assert(!strcmp(restored.snapshot.place, "San Francisco") && restored.snapshot.updated_utc == NOW);
    assert(air_snapshot_age(&restored, 0) == -1 && air_snapshot_age(&restored, NOW + 3600) == 3600);
    cJSON_SetNumberValue(cJSON_GetObjectItem(p, "version"), 2);
    assert(!air_core_restore(&restored, p) && restored.view == AIR_NIGHT); cJSON_Delete(p);
    const char *bad[] = {"[]", "{}", "{\"version\":1,\"view\":\"auto\",\"snapshot\":null}",
        "{\"version\":1,\"view\":\"ambient\",\"snapshot\":{}}",
        "{\"version\":1,\"version\":1,\"view\":\"ambient\",\"snapshot\":null}",
        "{\"version\":1,\"view\":\"ambient\",\"snapshot\":null,\"extra\":1}"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        p = cJSON_Parse(bad[i]); assert(!air_core_restore(&restored, p)); cJSON_Delete(p);
        assert(restored.snapshot.aqi == 42 && restored.view == AIR_NIGHT);
    }
    reject_save = true; air_core_t before = c;
    replacement("aqi", "43", false); run("air.set_view", "{\"view\":\"numbers\"}", false);
    assert(!memcmp(&c, &before, sizeof(c)) && saved.view == AIR_NIGHT && saved.snapshot.aqi == 42);
    init(); run("air.set_view", "{\"view\":\"night\"}", true);
    p = air_core_json(&c); assert(air_core_restore(&restored, p) && !restored.snapshot.present && restored.view == AIR_NIGHT);
    cJSON_Delete(p);
}
static void views(void) {
    init();
    for (int i = 0; i < AIR_VIEW_COUNT; i++) {
        char json[80]; snprintf(json, sizeof(json), "{\"view\":\"%s\"}", air_view_names[i]);
        run("air.set_view", json, true); assert((int)c.view == i);
    }
    const char *bad[] = {NULL, "{}", "[]", "{\"view\":\"auto\"}", "{\"view\":1}",
        "{\"view\":\"night\",\"x\":1}", "{\"view\":\"night\",\"view\":\"night\"}"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) run("air.set_view", bad[i], false);
    assert(c.view == AIR_NIGHT);
}
static void status(void) {
    init(); cJSON *r = command("air.status", "{}", NOW, true);
    cJSON *p = cJSON_GetObjectItem(r, "payload");
    assert(cJSON_IsNull(cJSON_GetObjectItem(p, "snapshot")) && cJSON_IsNull(cJSON_GetObjectItem(p, "age_s")));
    cJSON_Delete(r); run("air.update", GOOD, true);
    const int64_t times[] = {0, NOW - 1, NOW, NOW + AIR_STALE_S - 1, NOW + AIR_STALE_S};
    const int64_t ages[] = {-1, -1, 0, AIR_STALE_S - 1, AIR_STALE_S};
    for (int i = 0; i < 5; i++) {
        r = command("air.status", NULL, times[i], true); p = cJSON_GetObjectItem(r, "payload");
        cJSON *age = cJSON_GetObjectItem(p, "age_s");
        assert(ages[i] < 0 ? cJSON_IsNull(age) : age->valuedouble == ages[i]);
        assert(cJSON_IsTrue(cJSON_GetObjectItem(p, "refresh_needed")) == (ages[i] < 0 || ages[i] >= AIR_STALE_S));
        char *json = cJSON_PrintUnformatted(r); assert(strlen(json) < 1024); cJSON_free(json); cJSON_Delete(r);
    }
    assert(saves == 1); run("air.status", "{\"x\":1}", false); run("air.status", "[]", false);
    run("air.set_thresholds", "{}", false); run("sensors.history", "{}", false);
}
static void previews(void) {
    init(); const char *dir = getenv("CARD_PREVIEW_DIR"); if (dir) mkdir(dir, 0755);
    uint16_t *pixels = malloc((480 * 480 + 2) * 2); assert(pixels);
    pixels[0] = pixels[480 * 480 + 1] = 0x1234; card_rgb565_t canvas;
    assert(!card_rgb565_canvas(&canvas, pixels + 1, 1, 480, 480));
    assert(card_rgb565_canvas(&canvas, pixels + 1, 480 * 480 * 2, 480, 480));
    for (int i = 0; i < AIR_VIEW_COUNT; i++) { c.view = (air_view_t)i; air_guardian_render(&canvas, &c, 0, -1); }
    run("air.update", GOOD, true);
    const char *names[] = {"good", "moderate", "unhealthy", "night"};
    const char *advice[] = {"Enjoy your usual plans.", "Check your local forecast.", "Consider a shorter walk.", "Next update in the morning."};
    const int aqi[] = {42, 78, 172, 42};
    for (int i = 0; i < 4; i++) {
        c.view = i == 1 ? AIR_NUMBERS : i == 3 ? AIR_NIGHT : AIR_AMBIENT;
        c.snapshot.category = i == 1 ? AIR_MODERATE : i == 2 ? AIR_UNHEALTHY : AIR_GOOD;
        c.snapshot.aqi = aqi[i]; c.snapshot.pm25 = i == 1 ? 24.6 : i == 2 ? 88.2 : 7.1;
        strcpy(c.snapshot.advice, advice[i]); air_guardian_render(&canvas, &c, NOW + 600, 20 * 60 + 10);
        assert(pixels[1] != 0 && pixels[0] == 0x1234 && pixels[480 * 480 + 1] == 0x1234);
        if (i == 1) {
            for (int x = 24; x < 480; x += 228) {
                unsigned ink = 0;
                for (int y = 239; y < 329; y++) for (int px = x; px < x + 204; px++)
                    ink += pixels[1 + y * 480 + px] == card_rgb565_color(231, 239, 235);
                assert(ink > 100);
            }
        }
        if (!dir) continue;
        char path[4096]; snprintf(path, sizeof(path), "%s/outdoor_%s.ppm", dir, names[i]);
        FILE *f = fopen(path, "wb"); assert(f); fputs("P6\n480 480\n255\n", f);
        for (int n = 1; n <= 480 * 480; n++) {
            uint16_t p = pixels[n];
            unsigned char rgb[] = {(unsigned char)(((p >> 11) & 31) * 255 / 31),
                (unsigned char)(((p >> 5) & 63) * 255 / 63), (unsigned char)((p & 31) * 255 / 31)};
            assert(fwrite(rgb, 1, 3, f) == 3);
        }
        fclose(f);
    }
    c.snapshot.aqi = -1; c.snapshot.pm25 = 1000;
    memset(c.snapshot.advice, 'A', AIR_ADVICE_BYTES - 1); memset(c.snapshot.place, 'W', AIR_PLACE_BYTES - 1);
    for (int category = 0; category < AIR_CATEGORY_COUNT; category++) {
        c.snapshot.category = (air_category_t)category;
        for (int view = 0; view < AIR_VIEW_COUNT; view++) {
            c.view = (air_view_t)view; air_guardian_render(&canvas, &c, 0, -1);
            air_guardian_render(&canvas, &c, NOW + 86400, 1);
        }
    }
    assert(pixels[0] == 0x1234 && pixels[480 * 480 + 1] == 0x1234); free(pixels);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "validation")) validation();
    else if (!strcmp(argv[1], "boundaries")) boundaries();
    else if (!strcmp(argv[1], "persistence")) persistence();
    else if (!strcmp(argv[1], "views")) views();
    else if (!strcmp(argv[1], "status")) status();
    else if (!strcmp(argv[1], "previews")) previews();
    else assert(false);
    puts("ok"); return 0;
}
