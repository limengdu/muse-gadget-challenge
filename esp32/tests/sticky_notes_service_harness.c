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
#include "sticky_notes.c"
static int64_t now_us;
static bool online = true, send_ok = true, nvs_fail, queue_full, event_ok = true;
static sticky_notes_model_t disk, staged;
static bool disk_exists;
static unsigned writes, events, cancels, results, redraws;
static void observed_redraw(void) { redraws++; }
static char last_event[256], last_body[2048];
static noise_ctrl_req_cb callback;
static void *callback_ctx;
static job_t queued;
static bool queued_valid;
static int64_t request_seq;
int64_t esp_timer_get_time(void) { return now_us; }
bool gadget_clock_valid(void) { return true; }
int64_t gadget_clock_epoch(void) { return 1704067200LL; }
int gadget_clock_minutes_of_day(void) { return 480; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t lock, TickType_t wait) { (void)lock; (void)wait; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t lock) { (void)lock; return pdTRUE; }
QueueHandle_t xQueueCreate(unsigned length, unsigned size) { assert(length == 4 && size == sizeof(job_t)); return (void *)1; }
BaseType_t xQueueSend(QueueHandle_t q, const void *job, int wait) {
    (void)q; (void)wait; if (queue_full || queued_valid) return pdFALSE;
    queued = *(const job_t *)job; queued_valid = true; return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *job, int wait) {
    (void)q; (void)wait; if (!queued_valid) return pdFALSE;
    *(job_t *)job = queued; queued_valid = false; return pdTRUE;
}
BaseType_t xTaskCreate(TaskFunction_t task, const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *handle) {
    (void)task; (void)name; (void)stack; (void)arg; (void)priority; *handle = (void *)1; return pdPASS;
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) { (void)clear; (void)wait; return 0; }
void xTaskNotifyGive(TaskHandle_t task) { (void)task; }
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *handle) {
    assert(!strcmp(ns, STORE_NAMESPACE)); *handle = 1;
    return mode == NVS_READONLY && !disk_exists ? ESP_ERR_NVS_NOT_FOUND : ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t len) {
    (void)h; assert(!strcmp(key, STORE_KEY) && len == sizeof(disk));
    staged = *(const sticky_notes_model_t *)value; return nvs_fail ? ESP_FAIL : ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len) {
    (void)h; assert(!strcmp(key, STORE_KEY) && *len >= sizeof(disk));
    if (!disk_exists) return ESP_ERR_NVS_NOT_FOUND;
    memcpy(out, &disk, sizeof(disk)); *len = sizeof(disk); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    (void)h; if (nvs_fail) return ESP_FAIL; disk = staged; disk_exists = true; writes++; return ESP_OK;
}
bool gadget_events_post(const char *source, const char *summary, const cJSON *data) {
    assert(!strcmp(source, "fridge-notes") && !data);
    if (!event_ok) return false;
    events++; snprintf(last_event, sizeof(last_event), "%s", summary); return true;
}
int64_t noise_ctrl_req_open(const char *verb, const char *path, const char *const *headers, bool end,
                            noise_ctrl_req_cb cb, void *ctx) {
    assert(!strcmp(verb, "POST") && !strcmp(path, "/fs/write") && !end && headers);
    if (!online) return 0;
    callback = cb; callback_ctx = ctx; return ++request_seq;
}
bool noise_ctrl_req_send(int64_t id, const void *body, size_t len, bool end, int wait) {
    assert(id && end && wait == 0 && len < sizeof(last_body));
    memcpy(last_body, body, len); last_body[len] = 0; return send_ok;
}
void noise_ctrl_req_cancel(int64_t id) { assert(id); cancels++; }
void noise_ctrl_send_command_result(noise_ctrl_session_generation_t generation, const char *id, cJSON *result) {
    assert(generation == 7 && !strcmp(id, "request")); results++; cJSON_Delete(result);
}
static void execute(const char *command, const char *params) {
    cJSON *p = cJSON_Parse(params);
    cJSON *result = sticky_notes_command(command, p, "request", 7);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(result, "_async"))); cJSON_Delete(result); cJSON_Delete(p);
    assert(queued_valid); job_t job; assert(xQueueReceive(s_jobs, &job, 0)); run_job(&job);
}
static void touch(bool clear, uint32_t id) {
    assert(sticky_notes_touch("shopping", id, clear));
    job_t job; assert(xQueueReceive(s_jobs, &job, 0)); run_job(&job);
}
static void ack(int status) { callback(callback_ctx, status, NULL, 0, true); }
static void persistence(void) {
    sticky_notes_watch(observed_redraw);
    assert(sticky_notes_init() && redraws == 1);
    execute("notes.add", "{\"list\":\"shopping\",\"items\":[\"Eggs\",\"Milk\"]}");
    assert(writes == 1 && results == 1 && disk_exists);
    sticky_notes_model_t restored; sticky_notes_model_init(&restored);
    assert(load(&restored) && !memcmp(&restored, s_model, sizeof(restored)));
    nvs_fail = true; touch(false, 1);
    assert(!s_model->lists[0].items[0].done && s_save_failed && !s_event.count);
    nvs_fail = false; touch(false, 1); assert(s_model->lists[0].items[0].done);
    assert(load(&restored) && restored.lists[0].items[0].done);
    queue_full = true; assert(!sticky_notes_touch("shopping", 1, false));
    cJSON *busy = sticky_notes_command("notes.get", NULL, "request", 7);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(busy, "ok"))); cJSON_Delete(busy); queue_full = false;
    disk.count = STICKY_NOTES_LISTS + 1; assert(!load(&restored));
    disk = *s_model; memset(disk.lists[0].items[0].text, 'x', STICKY_NOTES_TEXT); assert(!load(&restored));
}
static void coalescing(void) {
    assert(sticky_notes_init());
    execute("notes.set", "{\"list\":\"shopping\",\"title\":\"Shopping\",\"items\":[\"Eggs\",\"Milk\"]}");
    touch(false, 1); event_step(2999999); assert(events == 0);
    now_us = 2000000; touch(false, 2); event_step(4000000); assert(!events);
    now_us = 5000000; event_step(now_us); assert(events == 1 && strstr(last_event, "2 touch changes"));
    touch(true, 0); now_us += DEBOUNCE_US; event_step(now_us);
    assert(events == 2 && !strcmp(last_event, "Cleared 2 done items from Shopping."));
    execute("notes.add", "{\"list\":\"shopping\",\"items\":[\"Eggs\"]}");
    touch(false, 3); now_us += DEBOUNCE_US; event_step(now_us);
    assert(!strcmp(last_event, "Ticked \"Eggs\" on the Shopping list."));
    touch(false, 3); event_ok = false; now_us += DEBOUNCE_US; event_step(now_us); assert(s_event.count);
    event_ok = true; now_us += DEBOUNCE_US; event_step(now_us); assert(!s_event.count);
    unsigned delivered = events;
    int64_t start = now_us;
    for (unsigned i = 0; i < 11; i++) {
        now_us = start + i * 1000000LL;
        touch(false, 3); event_step(now_us);
    }
    assert(events == delivered + 1 && !s_event.count);

}
static void synchronization(void) {
    assert(sticky_notes_init());
    execute("notes.add", "{\"list\":\"shopping\",\"items\":[\"Eggs\"]}");
    mirror_step(2999999); assert(!s_request);
    online = false; now_us = DEBOUNCE_US; mirror_step(now_us); assert(!s_request && s_retry_at > now_us);
    online = true; mirror_step(now_us + 1); assert(!s_request);
    now_us = s_retry_at; mirror_step(now_us); assert(s_request);
    cJSON *body = cJSON_Parse(last_body); assert(cJSON_IsTrue(cJSON_GetObjectItem(body, "overwrite"))); cJSON_Delete(body);
    ack(500); mirror_step(now_us); assert(!s_request && !s_synced_revision);
    now_us = s_retry_at; mirror_step(now_us); ack(200); mirror_step(now_us);
    assert(s_synced_revision == s_model->revision && s_synced_minute == 480);
    sticky_notes_view_t view; assert(sticky_notes_view(0, &view) && !view.pending);
    touch(false, 1); now_us += DEBOUNCE_US; mirror_step(now_us); assert(s_request);
    noise_ctrl_req_cb old_cb = callback; void *old_ctx = callback_ctx;
    now_us += REQUEST_US; mirror_step(now_us); assert(cancels && !s_request);
    now_us = s_retry_at; mirror_step(now_us); assert(s_request);
    old_cb(old_ctx, 200, NULL, 0, true); assert(!(atomic_load(&s_rx_state) & RX_DONE));
    ack(200); mirror_step(now_us); assert(s_synced_revision == s_model->revision);
    touch(false, 1); now_us += DEBOUNCE_US; send_ok = false; mirror_step(now_us);
    assert(!s_request && s_retry_at > now_us);
    send_ok = true;
    cJSON *params = cJSON_Parse("{\"list\":\"shopping\",\"items\":[]}");
    cJSON *items = cJSON_GetObjectItem(params, "items");
    char long_text[128]; memset(long_text, 'x', 127); long_text[127] = 0;
    for (unsigned i = 0; i < 20; i++) cJSON_AddItemToArray(items, cJSON_CreateString(long_text));
    char *text = cJSON_PrintUnformatted(params); cJSON_Delete(params);
    execute("notes.set", text); free(text);
    now_us += DEBOUNCE_US; mirror_step(now_us); assert(s_request);
    uint32_t snapshot_revision = s_mirror_revision;
    touch(false, s_model->lists[0].items[0].id);
    unsigned chunks = 0;
    char combined[20000] = {0};
    while (s_request) {
        assert(chunks < 100);
        cJSON *part = cJSON_Parse(last_body);
        assert(cJSON_IsTrue(cJSON_GetObjectItem(part, "append")) == (chunks != 0));
        assert(cJSON_IsTrue(cJSON_GetObjectItem(part, "overwrite")) == (chunks == 0));
        const char *piece = cJSON_GetStringValue(cJSON_GetObjectItem(part, "text"));
        assert(strlen(combined) + strlen(piece) < sizeof(combined));
        strcat(combined, piece); cJSON_Delete(part);
        ack(200); mirror_step(now_us); chunks++;
    }
    assert(chunks > 1 && s_synced_revision == snapshot_revision && s_model->revision > snapshot_revision);
    params = cJSON_Parse(combined); assert(params);
    assert(cJSON_GetObjectItem(params, "revision")->valuedouble == snapshot_revision); cJSON_Delete(params);
    assert(sticky_notes_view(0, &view) && view.pending);
    now_us += DEBOUNCE_US; mirror_step(now_us);
    for (unsigned i = 0; s_request; i++) { assert(i < 100); ack(200); mirror_step(now_us); }
    assert(s_synced_revision == s_model->revision);

}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "persistence")) persistence();
    else if (!strcmp(argv[1], "events")) coalescing();
    else synchronization();
    puts("ok");
}
