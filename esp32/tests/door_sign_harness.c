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

#include "../main/door_sign.c"

static char storage[DOOR_SIGN_STORAGE_BYTES];
static bool store_ok = true, event_ok = true, queue_ok = true;
static unsigned writes, events, results;
static int64_t epoch = 1800000000, uptime = 0;
static int local_second = 18 * 3600 + 32 * 60;
static bool clock_ok = true;
static char last_event[512];
static cJSON *last_result;
static void *queue[8];
static unsigned queue_count;

bool config_get_str(const char *key, char *out, size_t capacity)
{
    assert(!strcmp(key, SIGN_KEY));
    if (!storage[0] || strlen(storage) >= capacity) return false;
    strcpy(out, storage);
    return true;
}
bool config_set_str(const char *key, const char *value)
{
    assert(!strcmp(key, SIGN_KEY));
    writes++;
    if (!store_ok) return false;
    assert(strlen(value) < sizeof(storage));
    strcpy(storage, value);
    return true;
}
int64_t esp_timer_get_time(void) { return uptime; }
int64_t gadget_clock_epoch(void) { return epoch; }
bool gadget_clock_local(struct tm *out)
{
    memset(out, 0, sizeof(*out));
    out->tm_hour = local_second / 3600;
    out->tm_min = local_second / 60 % 60;
    out->tm_sec = local_second % 60;
    return clock_ok;
}
bool gadget_events_post(const char *source, const char *summary, const cJSON *data)
{
    assert(!strcmp(source, "door-sign") && cJSON_IsObject(data));
    if (!event_ok) return false;
    events++;
    strcpy(last_event, summary);
    return true;
}
void noise_ctrl_send_command_result(noise_ctrl_session_generation_t generation, const char *id, cJSON *result)
{
    assert(generation == 7 && !strcmp(id, "request-1"));
    results++;
    cJSON_Delete(last_result);
    last_result = result;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t lock, TickType_t wait) { (void)lock; (void)wait; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t lock) { (void)lock; return pdTRUE; }
QueueHandle_t xQueueCreate(unsigned length, unsigned size) { assert(length == 8 && size == sizeof(void *)); return (void *)2; }
void vQueueDelete(QueueHandle_t q) { (void)q; }
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
    (void)q;
    assert(wait == 0);
    if (!queue_ok || queue_count == 8) return pdFALSE;
    queue[queue_count++] = *(void *const *)item;
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
    (void)q; (void)wait;
    if (!queue_count) return pdFALSE;
    *(void **)item = queue[0];
    memmove(queue, queue + 1, --queue_count * sizeof(void *));
    return pdTRUE;
}
BaseType_t xTaskCreate(TaskFunction_t task, const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *out)
{
    (void)task; (void)name; (void)stack; (void)arg; (void)priority; (void)out;
    return pdPASS;
}

static cJSON *invoke(const char *command, const char *params)
{
    cJSON *p = params ? cJSON_Parse(params) : NULL;
    cJSON *r = door_sign_command(command, p, "request-1", 7);
    cJSON_Delete(p);
    return r;
}
static void drain(void)
{
    sign_job_t *job;
    while (xQueueReceive(s_jobs, &job, 0)) { process_job(job); free(job); }
}
static void expect_bad(const char *json)
{
    cJSON *r = invoke("sign.set", json);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok")));
    cJSON_Delete(r);
}
static void set(const char *json)
{
    cJSON *r = invoke("sign.set", json);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(r, "_async")));
    cJSON_Delete(r);
    drain();
    assert(cJSON_IsTrue(cJSON_GetObjectItem(last_result, "ok")));
}

int main(void)
{
    assert(door_sign_init());
    door_sign_snapshot_t view;
    assert(door_sign_snapshot(&view) && !view.sign.active);
    const char *states[] = {"meeting", "focus", "free", "away", "custom"};
    const char *titles[] = {"In a meeting", "Focusing", "Come in", "Away", "My door"};
    const uint32_t colors[] = {0xb52b36, 0x9b6500, 0x167347, 0x245eaa, 0x6943a3};
    for (unsigned i = 0; i < 5; i++) {
        char json[64]; snprintf(json, sizeof(json), "{\"state\":\"%s\"}", states[i]);
        set(json);
        assert(s_current.active && !strcmp(s_current.title, titles[i]) && s_current.color == colors[i]);
        assert(s_current.reply_count == 3);
    }
    const char *bad[] = {
        "null", "[]", "{}", "{\"state\":1}", "{\"state\":\"busy\"}",
        "{\"state\":\"free\",\"bogus\":1}", "{\"state\":\"free\",\"state\":\"away\"}",
        "{\"state\":\"free\",\"color\":\"#ffffff\"}",
        "{\"state\":\"custom\",\"color\":\"#fffffg\"}",
        "{\"state\":\"custom\",\"color\":123}",
        "{\"state\":\"free\",\"owner\":\" \"}", "{\"state\":\"free\",\"title\":null}",
        "{\"state\":\"free\",\"title\":\"\"}", "{\"state\":\"free\",\"subtitle\":4}",
        "{\"state\":\"free\",\"title\":\"12345678901234567890123456789012345678901\"}",
        "{\"state\":\"free\",\"subtitle\":\"line\\nline\"}",
        "{\"state\":\"free\",\"replies\":[\"a\",\"b\",\"c\",\"d\"]}",
        "{\"state\":\"free\",\"replies\":[\"a\",\"a\"]}",
        "{\"state\":\"free\",\"replies\":[1]}", "{\"state\":\"free\",\"replies\":[\"\"]}",
        "{\"state\":\"free\",\"replies\":null}",
        "{\"state\":\"meeting\",\"until\":\"24:00\"}",
        "{\"state\":\"meeting\",\"until\":\"12:60\"}",
        "{\"state\":\"meeting\",\"until\":\"3:00\"}",
        "{\"state\":\"meeting\",\"until\":true}",
        "{\"state\":\"meeting\",\"until\":1800000000.5}",
        "{\"state\":\"meeting\",\"until\":1800000000}",
        "{\"state\":\"meeting\",\"until\":4102444800}",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) expect_bad(bad[i]);
    expect_bad("{\"state\":\"free\",\"owner\":\"\xc0\xaf\"}");
    clock_ok = false;
    expect_bad("{\"state\":\"focus\",\"until\":\"15:00\"}");
    set("{\"state\":\"focus\",\"replies\":[]}");
    assert(s_current.reply_count == 0);
    clock_ok = true;
    set("{\"state\":\"focus\",\"until\":\"18:32\"}");
    assert(s_current.until == epoch + 86400);
    door_sign_model_t exact = s_current;
    assert(!door_sign_expire(&exact, (door_sign_time_t){.epoch = exact.until - 1, .valid = true}));
    assert(door_sign_expire(&exact, (door_sign_time_t){.epoch = exact.until, .valid = true}));
    assert(exact.state == DOOR_SIGN_FREE && !strcmp(exact.owner, "Dad"));
    set("{\"state\":\"meeting\",\"until\":1800000060}");
    assert(s_current.until == epoch + 60);
    local_second = 23 * 3600 + 59 * 60 + 30;
    set("{\"state\":\"meeting\",\"owner\":\"Mum\",\"until\":\"00:01\"}");
    assert(s_current.until == epoch + 90);
    door_sign_model_t restored;
    assert(door_sign_decode(storage, &restored) && restored.until == epoch + 90 && !strcmp(restored.owner, "Mum"));
    clock_ok = false;
    epoch += 100;
    check_expiry();
    assert(s_current.state == DOOR_SIGN_MEETING);
    clock_ok = true;
    store_ok = false;
    check_expiry();
    assert(s_current.state == DOOR_SIGN_MEETING);
    store_ok = true;
    event_ok = false;
    check_expiry();
    assert(s_current.state == DOOR_SIGN_FREE && !s_current.until && s_expiry_event[0]);
    unsigned before = events;
    event_ok = true;
    check_expiry(); check_expiry();
    assert(events == before + 1 && !strcmp(last_event, "Mum's door sign switched back to \"Come in\"."));
    assert(door_sign_decode(storage, &restored) && restored.state == DOOR_SIGN_FREE);
    local_second = 18 * 3600 + 32 * 60;
    set("{\"state\":\"custom\",\"owner\":\"Dad\",\"title\":\"Quiet please\",\"subtitle\":\"Back soon\",\"color\":\"#ffffff\",\"replies\":[\"Dinner is ready\",\"Need you\"]}");
    assert(door_sign_decode(storage, &restored) && !memcmp(&restored, &s_current, sizeof(restored)));
    assert(s_current.color == 0xffffff && s_current.reply_count == 2);
    s_jobs = NULL;
    memset(&s_current, 0, sizeof(s_current));
    assert(door_sign_init() && !memcmp(&restored, &s_current, sizeof(restored)));
    before = events;
    assert(door_sign_reply(0, s_view.revision)); drain();
    assert(events == before + 1 && !strcmp(last_event, "Someone at Dad's door tapped \"Dinner is ready\" (18:32)."));
    assert(door_sign_snapshot(&view) && view.sent_reply == 0 && view.sent_us == uptime);
    uptime = 9999999;
    assert(door_sign_reply(1, s_view.revision)); drain();
    assert(events == before + 1);
    uptime = 10000000;
    assert(door_sign_reply(1, s_view.revision)); drain();
    assert(events == before + 2);
    uptime += 10000000;
    event_ok = false;
    assert(door_sign_reply(0, s_view.revision)); drain();
    assert(s_last_tap_us == 10000000);
    event_ok = true;
    assert(door_sign_reply(0, s_view.revision)); drain();
    assert(events == before + 3);
    assert(door_sign_reply(0, s_view.revision - 1)); drain();
    assert(events == before + 3);
    assert(door_sign_reply(2, s_view.revision)); drain();
    assert(events == before + 3);
    store_ok = false;
    cJSON *r = invoke("sign.clear", "{}"); cJSON_Delete(r); drain();
    assert(s_current.active && cJSON_IsFalse(cJSON_GetObjectItem(last_result, "ok")));
    store_ok = true;
    r = invoke("sign.clear", NULL); cJSON_Delete(r); drain();
    assert(!s_current.active && door_sign_decode(storage, &restored) && !restored.active);
    s_jobs = NULL;
    assert(door_sign_init() && !s_current.active);
    r = invoke("sign.get", NULL);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(cJSON_GetObjectItem(r, "payload"), "active")));
    cJSON_Delete(r);
    r = invoke("sign.get", "{\"state\":\"free\"}");
    assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok"))); cJSON_Delete(r);
    queue_ok = false;
    expect_bad("{\"state\":\"free\"}");
    assert(!door_sign_reply(0, s_view.revision));
    assert(!door_sign_decode("{\"version\":2}", &restored));
    assert(!door_sign_decode("broken", &restored));
    cJSON *commands = cJSON_CreateObject(); door_sign_register(commands);
    assert(cJSON_GetObjectItem(commands, "sign.set") && cJSON_GetObjectItem(commands, "sign.get") && cJSON_GetObjectItem(commands, "sign.clear"));
    char *registration = cJSON_PrintUnformatted(commands);
    assert(strlen(registration) < 1600); free(registration); cJSON_Delete(commands);
    cJSON_Delete(last_result);
    assert(writes >= 10 && results >= 9);
    puts("Door sign: defaults, validation, midnight, expiry, debounce, events, persistence and command dispatch passed");
    return 0;
}
