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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "home_eyes_core.h"

static bool run(home_eyes_state_t *s, const char *cmd, const char *json, int64_t now, int64_t utc)
{
    cJSON *p = json ? cJSON_Parse(json) : NULL;
    bool changed, chime;
    cJSON *r = home_eyes_run(s, cmd, p, now, utc, &changed, &chime);
    bool ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok"));
    if (!strcmp(cmd, "home.say") && ok) assert(chime && changed);
    cJSON_Delete(p); cJSON_Delete(r); return ok;
}

static void diff(void)
{
    uint8_t a[HOME_EYES_PIXELS], b[HOME_EYES_PIXELS];
    memset(a, 80, sizeof(a)); memset(b, 80, sizeof(b));
    assert(home_eyes_difference(a, b) == 0);
    memset(b, 120, sizeof(b)); assert(home_eyes_difference(a, b) == 0);
    for (int i = 0; i < HOME_EYES_PIXELS; i++) b[i] = 80 + (i % 5) - 2;
    assert(home_eyes_difference(a, b) < 2);
    for (int amp = 12; amp <= 48; amp *= 2) {
        for (int y = 0; y < HOME_EYES_SIDE; y++) for (int x = 0; x < HOME_EYES_SIDE; x++)
            b[y * HOME_EYES_SIDE + x] = 80 + (x < 16 ? amp : 0);
        unsigned d = home_eyes_difference(a, b);
        assert(d >= (unsigned)(amp / 2 - 1) && d <= (unsigned)(amp / 2));
        for (unsigned sensitivity = 0; sensitivity < 3; sensitivity++) {
            home_eyes_state_t s; home_eyes_reset(&s);
            s.active = true; s.sensitivity = sensitivity;
            home_eyes_frame(&s, a, 0);
            unsigned threshold[] = {18, 10, 5};
            assert(home_eyes_frame(&s, b, 600000) == (d >= threshold[sensitivity]));
        }
    }
}

static void flows(void)
{
    home_eyes_state_t s; home_eyes_reset(&s);
    uint8_t a[HOME_EYES_PIXELS], b[HOME_EYES_PIXELS];
    memset(a, 80, sizeof(a));
    for (int i = 0; i < HOME_EYES_PIXELS; i++) b[i] = (i % 32 < 16) ? 180 : 80;
    assert(run(&s, "watch.start", "{\"name\":\"printer\",\"question\":\"Has the print failed?\",\"interval_min\":2,\"until\":1800001000}", 0, 1800000000));
    assert(s.active && home_eyes_due(&s, 0));
    assert(!home_eyes_frame(&s, a, 0));
    assert(!home_eyes_due(&s, 119999)); assert(home_eyes_due(&s, 120000));
    assert(!home_eyes_frame(&s, a, 120000));
    assert(home_eyes_frame(&s, b, 240000));
    assert(s.events & HOME_EYES_CHANGE);
    assert(!home_eyes_frame(&s, b, 480000));
    assert(!run(&s, "watch.start", "{\"name\":\"door\",\"question\":\"Parcel?\"}", 250000, 1800000250));
    assert(run(&s, "watch.status", NULL, 250000, 1800000250));
    assert(run(&s, "watch.verdict", "{\"text\":\"Print looks healthy.\",\"alert\":false}", 250000, 1800000250));
    assert(!s.waiting && !(s.events & HOME_EYES_CHANGE));
    assert(!home_eyes_frame(&s, b, 370000));
    assert(home_eyes_ack(&s, s.verdict_id)); assert(!s.verdict && !s.events);
    assert(home_eyes_frame(&s, a, 490000));
    assert(run(&s, "watch.verdict", "{\"text\":\"Print detached.\",\"alert\":true}", 500000, 1800000500));
    assert(s.alert && s.verdict && !home_eyes_due(&s, 900000));
    assert(run(&s, "home.say", "{\"text\":\"Home in 20 minutes.\",\"from\":\"Parent\"}", 510000, 1800000510));
    assert(!home_eyes_ack(&s, s.message_id + 100));
    assert(home_eyes_ack(&s, s.verdict_id)); assert(s.events & HOME_EYES_ALERT_ACK);
    assert(!home_eyes_ack(&s, s.verdict_id));
    assert(!home_eyes_reminder(&s, 1109999)); assert(home_eyes_reminder(&s, 1110000));
    assert(!home_eyes_reminder(&s, 1710000));
    char *saved = home_eyes_save(&s, 1110000, 1800001110);
    home_eyes_state_t restored; home_eyes_reset(&restored);
    assert(home_eyes_restore(&restored, saved, 0, 1800001120)); free(saved);
    assert(restored.reminded && restored.message && !restored.active);
    assert(!restored.reference_valid);
    assert(home_eyes_ack(&restored, restored.message_id));
    assert(strstr(restored.seen, "Home in 20")); assert(restored.events & HOME_EYES_MESSAGE_ACK);
    assert(!run(&restored, "home.say", "{\"text\":\"Second\",\"from\":\"Parent\"}", 0, 0));
    restored.events = 0;
    assert(run(&restored, "home.say", "{\"text\":\"Second\",\"from\":\"Parent\"}", 0, 0));
    assert(run(&s, "watch.stop", "{}", 1110000, 1800001110));
    assert(!s.active && !s.waiting);
}

static void validation(void)
{
    const char *bad[] = {
        "{}", "[]", "null", "{\"name\":true,\"question\":\"Q\"}",
        "{\"name\":\" \",\"question\":\"Q\"}",
        "{\"name\":\"N\",\"question\":\"Q\",\"interval_min\":1}",
        "{\"name\":\"N\",\"question\":\"Q\",\"interval_min\":121}",
        "{\"name\":\"N\",\"question\":\"Q\",\"interval_min\":2.5}",
        "{\"name\":\"N\",\"question\":\"Q\",\"sensitivity\":\"urgent\"}",
        "{\"name\":\"N\",\"question\":\"Q\",\"until\":1}",
        "{\"name\":\"N\",\"question\":\"Q\",\"until\":\"tomorrow\"}",
        "{\"name\":\"N\",\"name\":\"M\",\"question\":\"Q\"}",
        "{\"name\":\"N\",\"question\":\"Q\",\"extra\":true}"
    };
    home_eyes_state_t s; home_eyes_reset(&s);
    for (unsigned n = 0; n < sizeof(bad) / sizeof(bad[0]); n++)
        assert(!run(&s, "watch.start", bad[n], 0, 1800000000));
    assert(!run(&s, "watch.start", NULL, 0, 0));
    assert(!run(&s, "watch.start", "{\"name\":\"N\",\"question\":\"Q\",\"until\":1800001000}", 0, 0));
    assert(!run(&s, "home.say", "{\"text\":\"X\"}", 0, 0));
    assert(!run(&s, "home.say", "{\"text\":false,\"from\":\"X\"}", 0, 0));
    assert(!run(&s, "watch.verdict", "{\"text\":\"X\",\"alert\":1}", 0, 0));
    assert(!run(&s, "watch.verdict", "{\"text\":\"X\",\"alert\":false}", 0, 0));
    assert(!run(&s, "watch.status", "{\"name\":\"X\"}", 0, 0));
    assert(!run(&s, "watch.stop", "[]", 0, 0));
    assert(!run(&s, "other", NULL, 0, 0));
    assert(run(&s, "watch.start", "{\"name\":\"N\",\"question\":\"Q\"}", 0, 0));
    assert(s.interval_min == 10 && s.sensitivity == 1);
    char *saved = home_eyes_save(&s, 0, 0);
    home_eyes_state_t restored; home_eyes_reset(&restored);
    assert(home_eyes_restore(&restored, saved, 5000, 0)); free(saved);
    assert(restored.active && restored.next_ms == 5000);
    assert(!home_eyes_restore(&restored, "{\"version\":2}", 0, 0));
    assert(!home_eyes_restore(&restored, "garbage", 0, 0));
    restored.verdict = true; restored.verdict_id = 42;
    restored.message = true; restored.message_id = 43;
    assert(home_eyes_ack(&restored, 42));
    assert(restored.message && !restored.verdict);
    char large[600]; memset(large, 'a', sizeof(large)); large[599] = 0;
    cJSON *p = cJSON_CreateObject(); cJSON_AddStringToObject(p, "text", large); cJSON_AddStringToObject(p, "from", "Parent");
    bool changed, chime; cJSON *result = home_eyes_run(&s, "home.say", p, 0, 0, &changed, &chime);
    assert(!cJSON_IsTrue(cJSON_GetObjectItem(result, "ok"))); cJSON_Delete(result); cJSON_Delete(p);
}

int main(void)
{
    diff(); flows(); validation();
    cJSON *commands = cJSON_CreateObject(); home_eyes_register(commands);
    assert(cJSON_GetArraySize(commands) == 5);
    char *json = cJSON_PrintUnformatted(commands);
    printf("Home Eyes command bytes: %zu\n", strlen(json));
    free(json); cJSON_Delete(commands);
    puts("Home Eyes core: OK"); return 0;
}
