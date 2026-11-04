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
#include <setjmp.h>
#include "gadget_events.c"

static int64_t now;
#ifdef LINK_FAKE_CUSTOM_CALLOC
static bool fail_calloc;
static unsigned psram_allocations, internal_allocations;
bool esp_psram_is_initialized(void) { return TEST_PSRAM_AVAILABLE; }
void *heap_caps_calloc(size_t count, size_t size, int caps) {
    if (caps & MALLOC_CAP_SPIRAM) psram_allocations++;
    else { assert(caps & MALLOC_CAP_INTERNAL); internal_allocations++; }
    if (fail_calloc) return NULL;
    return calloc(count, size);
}
#endif
static bool connected = true, fail_lock, fail_open, no_reply;
static unsigned opens, sends, cancels, delays, notifications;
static bool run_worker;
static jmp_buf worker_end;
static char sent_message[4096], post_body[8192];
static size_t post_length;
static noise_ctrl_req_cb callbacks[2];
static void *contexts[2];

int64_t esp_timer_get_time(void) { return now; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t wait) {
    (void)h;
    if (!wait && fail_lock) { fail_lock = false; return pdFALSE; }
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h) { (void)h; return pdTRUE; }
BaseType_t xTaskCreate(TaskFunction_t task, const char *name, unsigned stack,
                       void *arg, unsigned priority, TaskHandle_t *handle) {
    (void)task; (void)name; (void)stack; (void)arg; (void)priority;
    if (handle) *handle = (void *)1;
    return pdPASS;
}
void vTaskDelete(TaskHandle_t h) { (void)h; }
void xTaskNotifyGive(TaskHandle_t h) { (void)h; notifications++; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) {
    (void)clear; (void)wait;
    if (run_worker) longjmp(worker_end, 1);
    return 0;
}
void vTaskDelay(int ticks) {
    now += (int64_t)ticks * 1000;
    delays++;
    if (run_worker && !connected && delays >= 2) connected = true;
}
bool noise_ctrl_is_connected(void) { return connected; }
int64_t noise_ctrl_req_open(const char *verb, const char *path,
                            const char *const *headers, bool end,
                            noise_ctrl_req_cb callback, void *context) {
    opens++;
    assert(!strcmp(verb, "POST") && !end);
    assert(!strcmp(headers[0], "Content-Type") && !strcmp(headers[1], "application/json"));
    if (fail_open) { fail_open = false; return 0; }
    unsigned slot = !strcmp(path, "/chat/subscribe") ? 1 : 0;
    if (!slot) assert(!strcmp(path, "/chat/stream"));
    if (!slot) post_length = 0;
    callbacks[slot] = callback; contexts[slot] = context;
    return slot + 1;
}

static void frame(unsigned slot, const char *text, bool end) {
    callbacks[slot](contexts[slot], 200, (const uint8_t *)text, strlen(text), end);
}

bool noise_ctrl_req_send(int64_t id, const void *data, size_t len, bool end, int wait) {
    (void)wait;
    sends++;
    if (id == 2) { assert(end && len == 2 && !memcmp(data, "{}", 2)); frame(1, "", false); return true; }
    assert(len <= 1024 && post_length + len < sizeof(post_body));
    memcpy(post_body + post_length, data, len); post_length += len;
    if (!end) return true;
    cJSON *body = cJSON_ParseWithLength(post_body, post_length);
    assert(body && !strcmp(field(body, "output_modality"), "text"));
    utf8_copy(sent_message, sizeof(sent_message), field(body, "message"), false);
    cJSON_Delete(body);
    if (!no_reply) {
        // Early reply frames precede the POST acknowledgement.
        // 早到的回复帧先于 POST 确认到达。
        frame(1, "{\"type\":\"event\",\"event_name\":\"message.assistant\",\"payload\":{\"message_id\":\"a1\",\"reply_to_message_id\":\"n1\",\"display_", false);
        frame(1, "text\":\"All ready!\",\"content\":\"wrong\"}}\n", false);
    }
    frame(0, "{\"result\":{\"message_id\":\"n1\"}}", true);
    return true;
}
void noise_ctrl_req_cancel(int64_t id) { assert(id == 1 || id == 2); cancels++; }

static void reset(void) {
    event_t *event;
    while ((event = dequeue())) release(event);
    for (unsigned i = 0; i < s_history_count; i++) release(s_history[(s_history_head + i) % 32]);
    s_count = s_head = s_history_count = s_history_head = 0;
    s_last_seq = s_dropped = 0;
    connected = true;
    no_reply = fail_open = false;
    opens = sends = cancels = delays = 0;
    s_reply[0] = 0;
}

static cJSON *payload(cJSON *result) { return cJSON_GetObjectItemCaseSensitive(result, "payload"); }
static double number(const cJSON *json, const char *key) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(json, key);
    assert(cJSON_IsNumber(v)); return v->valuedouble;
}

static void queue_tests(void) {
    assert(!gadget_events_post("button", "early", NULL));
#ifdef LINK_FAKE_CUSTOM_CALLOC
    fail_calloc = true; assert(!gadget_events_init()); fail_calloc = false;
    if (TEST_PSRAM_AVAILABLE) assert(psram_allocations == 2 && internal_allocations == 0);
    else assert(psram_allocations == 0 && internal_allocations == 2);
#endif
    assert(gadget_events_init() && gadget_events_init());
    cJSON *data = cJSON_Parse("{\"pressed\":true,\"count\":2}");
    assert(gadget_events_post("button", "Ready \"now\"", data));
    cJSON_Delete(data);
    event_t *event = dequeue();
    assert(!strcmp(event->message, "[button event] Ready \"now\"\n{\"pressed\":true,\"count\":2}"));
    connected = false;
    assert(!send_event(event) && opens == 0);
    connected = true;
    assert(send_event(event));
    assert(!strcmp(sent_message, event->message));
    char reply[601]; gadget_events_reply(reply, sizeof(reply));
    assert(!strcmp(reply, "All ready!"));
    assert(cancels == 2);
    // A cancelled generation cannot change the next turn's reply.
    // 已取消代号的回调无法改写下一轮的回复。
    frame(1, "{\"type\":\"event\",\"event_name\":\"message.assistant\",\"payload\":{\"message_id\":\"a2\",\"reply_to_message_id\":\"n1\",\"text\":\"late\"}}\n", false);
    assert(!strcmp(s_rx_reply, "All ready!"));
    release(event);
    reset();
    char summary[512], value[901];
    memset(summary, 'a', sizeof(summary) - 1); summary[511] = 0;
    memset(value, 'b', sizeof(value) - 1); value[900] = 0;
    data = cJSON_CreateString(value);
    assert(gadget_events_post("large", summary, data)); cJSON_Delete(data);
    event = dequeue(); assert(send_event(event));
    assert(!strcmp(sent_message, event->message) && sends >= 3); release(event);
    reset();
    for (int i = 0; i < 40; i++) {
        char summary[32]; snprintf(summary, sizeof(summary), "event %d", i);
        assert(gadget_events_post("test", summary, NULL));
    }
    assert(s_count == 16 && s_history_count == 32 && s_dropped == 24);
    cJSON *result = gadget_events_poll(37), *p = payload(result);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(result, "ok")));
    assert(number(p, "last_seq") == 40 && number(p, "dropped") == 24);
    cJSON *events = cJSON_GetObjectItem(p, "events");
    assert(cJSON_GetArraySize(events) == 3);
    assert(number(cJSON_GetArrayItem(events, 0), "seq") == 38);
    assert(!strcmp(field(cJSON_GetArrayItem(events, 2), "summary"), "event 39"));
    cJSON_Delete(result);
    result = gadget_events_poll(0);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(payload(result), "events")) == 32);
    assert(number(cJSON_GetArrayItem(cJSON_GetObjectItem(payload(result), "events"), 0), "seq") == 9);
    cJSON_Delete(result);
    result = gadget_events_poll(100);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(payload(result), "events")) == 0);
    assert(number(payload(result), "last_seq") == 40);
    cJSON_Delete(result);
    const char *invalid[] = {"{\"since\":-1}", "{\"since\":1.5}", "{\"since\":\"2\"}", "{\"since\":1e100}"};
    for (unsigned i = 0; i < 4; i++) {
        cJSON *params = cJSON_Parse(invalid[i]);
        result = gadget_events_command(params);
        assert(cJSON_IsFalse(cJSON_GetObjectItem(result, "ok")));
        cJSON_Delete(result); cJSON_Delete(params);
    }
    fail_lock = true;
    assert(!gadget_events_post("test", "contended", NULL) && s_last_seq == 40);
    assert(!gadget_events_post(NULL, "x", NULL));
    assert(!gadget_events_post("x", "", NULL));
    reset();
}

static void parser_tests(void) {
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    memset(s_candidates, 0, 4 * sizeof(*s_candidates)); s_candidate_next = 0;
    strcpy(s_note_id, "my-note"); s_parent_id[0] = s_echo_id[0] = s_rx_reply[0] = 0;
    s_replied = false;
    xSemaphoreGive(s_rx_lock);
    int64_t sub = open_request(1, "/chat/subscribe");
    frame(1, "{\"type\":\"event\",\"event\":\"message.assistant\",\"payload\":{\"id\":\"other\",\"reply_to_message_id\":\"unrelated\",\"text\":\"Ignore me\"}}\n", false);
    assert(!s_replied);
    frame(1, "{\"type\":\"event\",\"event\":\"delta.message_start\",\"payload\":{\"id\":\"a\",\"parent_message_id\":\"my-note\"}}\n", false);
    frame(1, "{\"type\":\"event\",\"event\":\"delta.text_append\",\"payload\":{\"id\":\"a\",\"content\":\"Hello \"}}\n", false);
    frame(1, "{\"type\":\"event\",\"event\":\"delta.text_append\",\"payload\":{\"id\":\"a\",\"text\":\"Muse\"}}\n", false);
    assert(!s_replied);
    frame(1, "{\"type\":\"event\",\"event\":\"delta.message_done\",\"payload\":{\"id\":\"a\"}}\n", false);
    assert(s_replied && !strcmp(s_rx_reply, "Hello Muse"));
    s_replied = false;
    char *big = malloc(ROW_MAX + 80); assert(big); memset(big, 'x', ROW_MAX + 79); big[ROW_MAX + 79] = 0;
    frame(1, big, false); free(big);
    frame(1, "\n{\"type\":\"event\",\"event_name\":\"message.assistant\",\"payload\":{\"id\":\"b\",\"reply_to_message_id\":\"my-note\",\"content\":\"After overflow\"}}\n", false);
    assert(s_replied && !strcmp(s_rx_reply, "After overflow"));
    // Truncation retains complete UTF-8 sequences at the byte bound.
    // 在字节上限处截断时保留完整的 UTF-8 字符。
    char many[1000], clipped[601];
    for (int i = 0; i < 333; i++) memcpy(many + 3 * i, "\xE4\xB8\xAD", 3);
    many[999] = 0;
    utf8_copy(clipped, sizeof(clipped), many, false);
    assert(strlen(clipped) == 600 && !memcmp(clipped + 597, "\xE4\xB8\xAD", 3));
    close_requests(0, sub);
}

static void worker_tests(void) {
    reset();
    assert(gadget_events_post("test", "recover", NULL));
    connected = false; fail_open = true; run_worker = true;
    if (!setjmp(worker_end)) event_task(NULL);
    run_worker = false;
    assert(delays >= 3 && s_count == 0 && !strcmp(sent_message, "[test event] recover"));
    assert(s_history_count == 1 && s_dropped == 0);
    reset();
    assert(gadget_events_console_command("event=Console ready"));
    event_t *event = dequeue(); assert(event);
    assert(send_event(event)); release(event);
    assert(!gadget_events_console_command("other=x"));
    reset();
}

int main(void) {
    queue_tests(); parser_tests(); worker_tests(); reset();
    puts("ok");
    return 0;
}
