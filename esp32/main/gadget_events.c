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

#include "gadget_events.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sdkconfig.h"
#include "esp_heap_caps.h"
#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "noise_control.h"

#define SOURCE_MAX 48
#define SUMMARY_MAX 512
#define DATA_MAX 1024
#define ROW_MAX 12288
#define REPLY_TIMEOUT_US (90LL * 1000000)
#define REPLY_SETTLE_US (2LL * 1000000)
#define MAX_SEQ 9007199254740991ULL

typedef struct {
    uint64_t seq;
    int64_t time;
    unsigned refs;
    bool epoch, console;
    char *source, *summary, *data, *message;
    char storage[];
} event_t;

static SemaphoreHandle_t s_lock, s_rx_lock;
static TaskHandle_t s_task;
static atomic_bool s_ready;
static event_t *s_queue[GADGET_EVENTS_PENDING], *s_history[GADGET_EVENTS_HISTORY];
static unsigned s_head, s_count, s_history_head, s_history_count;
static uint64_t s_last_seq, s_dropped;
static char s_reply[GADGET_EVENTS_REPLY_BYTES];

// Immutable events are shared by the history, pending queue and sender.
// 不可变事件由历史记录、待发队列及发送任务共同持有。
static void release(event_t *event) {
    if (!--event->refs) free(event);
}

static void utf8_copy(char *out, size_t cap, const char *text, bool append) {
    if (!cap) return;
    size_t used = append ? strlen(out) : 0, n = strlen(text);
    if (n >= cap - used) {
        n = cap - used - 1;
        while (n && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
    }
    memcpy(out + used, text, n);
    out[used + n] = 0;
}

static void progress(const event_t *event, const char *status, int http, const char *text) {
    if (!event->console) return;
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", status);
    cJSON_AddNumberToObject(json, "seq", (double)event->seq);
    if (http) cJSON_AddNumberToObject(json, "http_status", http);
    if (text) cJSON_AddStringToObject(json, !strcmp(status, "reply") ? "text" : "message", text);
    char *line = cJSON_PrintUnformatted(json);
    if (line) {
        flockfile(stdout);
        printf("@event %s\n", line);
        fflush(stdout);
        funlockfile(stdout);
    }
    cJSON_free(line);
    cJSON_Delete(json);
}

// Allocates packed event strings and the formatted chat message together.
// 将事件字符串与格式化的聊天消息一起分配。
static event_t *make_event(const char *source, const char *summary, const cJSON *data) {
    if (!source || !source[0] || !summary || !summary[0]
        || strlen(source) >= SOURCE_MAX || strlen(summary) >= SUMMARY_MAX) return NULL;
    char *json = data ? cJSON_PrintUnformatted(data) : NULL;
    if ((data && !json) || (json && strlen(json) > DATA_MAX)) { cJSON_free(json); return NULL; }
    size_t ns = strlen(source) + 1, nt = strlen(summary) + 1, nd = json ? strlen(json) + 1 : 0;
    size_t message_cap = ns + nt + nd + 12;
    event_t *event = calloc(1, sizeof(*event) + ns + nt + nd + message_cap);
    if (!event) { cJSON_free(json); return NULL; }
    event->source = event->storage;
    event->summary = event->source + ns;
    event->data = json ? event->summary + nt : NULL;
    event->message = event->summary + nt + nd;
    memcpy(event->source, source, ns);
    memcpy(event->summary, summary, nt);
    if (json) memcpy(event->data, json, nd);
    snprintf(event->message, message_cap, "[%s event] %s%s%s", source, summary,
             json ? "\n" : "", json ? json : "");
    cJSON_free(json);
    int64_t epoch = (int64_t)time(NULL);
    event->epoch = epoch >= 1704067200LL && epoch <= 4102444799LL;
    event->time = event->epoch ? epoch : esp_timer_get_time() / 1000000;
    event->console = !strcmp(source, "console");
    return event;
}

static bool enqueue(const char *source, const char *summary, const cJSON *data, bool console) {
    if (!atomic_load(&s_ready)) return false;
    event_t *event = make_event(source, summary, data);
    if (!event) return false;
    if (xSemaphoreTake(s_lock, 0) != pdTRUE) { free(event); return false; }
    if (s_last_seq == MAX_SEQ) { xSemaphoreGive(s_lock); free(event); return false; }
    event->seq = ++s_last_seq;
    event->refs = console ? 3 : 2;
    if (s_count == GADGET_EVENTS_PENDING) {
        release(s_queue[s_head]);
        s_head = (s_head + 1) % GADGET_EVENTS_PENDING;
        s_count--;
        s_dropped++;
    }
    s_queue[(s_head + s_count++) % GADGET_EVENTS_PENDING] = event;
    if (s_history_count == GADGET_EVENTS_HISTORY) {
        release(s_history[s_history_head]);
        s_history_head = (s_history_head + 1) % GADGET_EVENTS_HISTORY;
        s_history_count--;
    }
    s_history[(s_history_head + s_history_count++) % GADGET_EVENTS_HISTORY] = event;
    xSemaphoreGive(s_lock);
    if (console) {
        progress(event, "queued", 0, NULL);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        release(event);
        xSemaphoreGive(s_lock);
    }
    xTaskNotifyGive(s_task);
    return true;
}

bool gadget_events_post(const char *source, const char *summary, const cJSON *data) {
    return enqueue(source, summary, data, false);
}

static event_t *dequeue(void) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    event_t *event = s_count ? s_queue[s_head] : NULL;
    if (event) { s_head = (s_head + 1) % GADGET_EVENTS_PENDING; s_count--; }
    xSemaphoreGive(s_lock);
    return event;
}

cJSON *gadget_events_poll(uint64_t since) {
    event_t *snapshot[GADGET_EVENTS_HISTORY];
    unsigned count = 0;
    uint64_t seq = 0, dropped = 0;
    if (atomic_load(&s_ready)) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        seq = s_last_seq;
        dropped = s_dropped;
        for (unsigned i = 0; i < s_history_count; i++) {
            event_t *event = s_history[(s_history_head + i) % GADGET_EVENTS_HISTORY];
            if (event->seq > since) { event->refs++; snapshot[count++] = event; }
        }
        xSemaphoreGive(s_lock);
    }
    cJSON *result = cJSON_CreateObject(), *payload = cJSON_CreateObject(), *events = cJSON_CreateArray();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddItemToObject(result, "payload", payload);
    cJSON_AddItemToObject(payload, "events", events);
    cJSON_AddNumberToObject(payload, "last_seq", (double)seq);
    cJSON_AddNumberToObject(payload, "dropped", (double)dropped);
    for (unsigned i = 0; i < count; i++) {
        event_t *event = snapshot[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "seq", (double)event->seq);
        cJSON_AddNumberToObject(item, "time", (double)event->time);
        cJSON_AddStringToObject(item, "time_basis", event->epoch ? "epoch" : "uptime");
        cJSON_AddStringToObject(item, "source", event->source);
        cJSON_AddStringToObject(item, "summary", event->summary);
        if (event->data) cJSON_AddItemToObject(item, "data", cJSON_Parse(event->data));
        cJSON_AddItemToArray(events, item);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        release(event);
        xSemaphoreGive(s_lock);
    }
    return result;
}

cJSON *gadget_events_command(const cJSON *params) {
    cJSON *since = cJSON_GetObjectItemCaseSensitive(params, "since");
    if (since && (!cJSON_IsNumber(since) || !isfinite(since->valuedouble)
        || since->valuedouble < 0 || since->valuedouble > (double)MAX_SEQ
        || floor(since->valuedouble) != since->valuedouble)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddBoolToObject(error, "ok", false);
        cJSON *details = cJSON_CreateObject();
        cJSON_AddStringToObject(details, "code", "invalid_params");
        cJSON_AddStringToObject(details, "message", "since must be a nonnegative integer sequence");
        cJSON_AddItemToObject(error, "error", details);
        return error;
    }
    return gadget_events_poll(since ? (uint64_t)since->valuedouble : 0);
}

void gadget_events_reply(char *out, size_t capacity) {
    if (!out || !capacity) return;
    out[0] = 0;
    if (!atomic_load(&s_ready)) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    utf8_copy(out, capacity, s_reply, false);
    xSemaphoreGive(s_lock);
}

bool gadget_events_console_command(const char *line) {
    if (!line || strncmp(line, "event=", 6)) return false;
    if (!enqueue("console", line + 6, NULL, true)) {
        event_t event = {.console = true};
        progress(&event, "error", 0, "event could not be queued");
    }
    return true;
}

// Request generations discard callbacks still arriving after cancellation.
// 请求代号用于丢弃取消后仍然到达的回调。
typedef struct {
    uint32_t generation;
    int status;
    bool done, overflow;
    size_t length;
    char row[ROW_MAX + 1];
} event_rx_t;
static event_rx_t *s_rx;
typedef struct {
    char id[96], parent[96], text[GADGET_EVENTS_REPLY_BYTES];
    bool final;
} event_candidate_t;
static event_candidate_t *s_candidates;
static unsigned s_candidate_next;
static char s_note_id[96], s_parent_id[96], s_echo_id[96];
static char s_rx_reply[GADGET_EVENTS_REPLY_BYTES];
static bool s_replied;
static int64_t s_reply_at;

static const char *field(const cJSON *object, const char *key) {
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, key));
    return s ? s : "";
}

static void select_reply(void) {
    if (!s_note_id[0]) return;
    for (unsigned i = 0; i < 4; i++) {
        if (!s_candidates[i].final || !s_candidates[i].text[0]) continue;
        const char *parent = s_candidates[i].parent;
        bool related = parent[0] ? !strcmp(parent, s_note_id)
            || (s_parent_id[0] && !strcmp(parent, s_parent_id))
            : !strcmp(s_echo_id, s_note_id) || (s_parent_id[0] && !strcmp(s_echo_id, s_parent_id));
        if (!related) continue;
        utf8_copy(s_rx_reply, sizeof(s_rx_reply), s_candidates[i].text, false);
        s_candidates[i].final = false;
        s_replied = true;
        s_reply_at = esp_timer_get_time();
    }
}

// Extracts acknowledgement IDs and assistant text from chat events.
// 从聊天事件中提取确认编号与助手回复正文。
static void parse_row(const char *row, size_t length) {
    cJSON *root = cJSON_ParseWithLength(row, length);
    if (!root) return;
    cJSON *ack = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (!cJSON_IsObject(ack)) ack = root;
    const char *id = field(ack, "message_id");
    if (strcmp(field(root, "type"), "event")) {
        if (id[0]) {
            utf8_copy(s_note_id, sizeof(s_note_id), id, false);
            utf8_copy(s_parent_id, sizeof(s_parent_id), field(ack, "reply_to_message_id"), false);
            select_reply();
        }
        cJSON_Delete(root);
        return;
    }
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!cJSON_IsObject(payload)) payload = root;
    const char *kind = field(root, "event_name");
    if (!kind[0]) kind = field(root, "event");
    if (!kind[0]) kind = field(payload, "event_name");
    if (!kind[0]) kind = field(payload, "event");
    id = field(payload, "message_id");
    if (!id[0]) id = field(payload, "id");
    bool user = !strcmp(kind, "message.user");
    bool append = !strcmp(kind, "delta.text_append");
    bool start = !strcmp(kind, "delta.message_start");
    bool done = !strcmp(kind, "delta.message_done");
    bool full = !strcmp(kind, "message.assistant");
    if (user && id[0]) utf8_copy(s_echo_id, sizeof(s_echo_id), id, false);
    if (id[0] && (append || start || done || full)) {
        unsigned slot = 0;
        while (slot < 4 && strcmp(s_candidates[slot].id, id)) slot++;
        if (slot == 4) {
            slot = s_candidate_next++ % 4;
            memset(&s_candidates[slot], 0, sizeof(s_candidates[slot]));
            utf8_copy(s_candidates[slot].id, sizeof(s_candidates[slot].id), id, false);
        }
        const char *parent = field(payload, "reply_to_message_id");
        if (!parent[0]) parent = field(payload, "parent_message_id");
        if (parent[0]) utf8_copy(s_candidates[slot].parent, sizeof(s_candidates[slot].parent), parent, false);
        const char *text = field(payload, "display_text");
        if (!text[0]) text = field(payload, "content");
        if (!text[0]) text = field(payload, "text");
        if (text[0]) utf8_copy(s_candidates[slot].text, sizeof(s_candidates[slot].text), text, append);
        s_candidates[slot].final = done || (full && !cJSON_IsFalse(
            cJSON_GetObjectItemCaseSensitive(payload, "display_text_ready")));
    }
    select_reply();
    cJSON_Delete(root);
}

// Splits bounded NDJSON rows across frames and drains oversized rows to newline.
// 跨帧拼接有界 NDJSON 行，并将超长行排空至换行符。
static void on_frame(void *ctx, int status, const uint8_t *data, size_t len, bool end) {
    uintptr_t token = (uintptr_t)ctx;
    unsigned slot = token & 1;
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    if ((uint32_t)(token >> 1) == s_rx[slot].generation && !s_rx[slot].done) {
        if (status) s_rx[slot].status = status;
        if (s_rx[slot].status == 200) {
            for (size_t i = 0; i < len; i++) {
                if (data[i] == '\n') {
                    if (!s_rx[slot].overflow && s_rx[slot].length) {
                        s_rx[slot].row[s_rx[slot].length] = 0;
                        parse_row(s_rx[slot].row, s_rx[slot].length + 1);
                    }
                    s_rx[slot].length = 0;
                    s_rx[slot].overflow = false;
                } else if (s_rx[slot].length < ROW_MAX && !s_rx[slot].overflow) {
                    s_rx[slot].row[s_rx[slot].length++] = (char)data[i];
                } else {
                    s_rx[slot].overflow = true;
                }
            }
            if (end && !s_rx[slot].overflow && s_rx[slot].length) {
                s_rx[slot].row[s_rx[slot].length] = 0;
                parse_row(s_rx[slot].row, s_rx[slot].length + 1);
                s_rx[slot].length = 0;
            }
        }
        s_rx[slot].done = end || status < 0;
    }
    xSemaphoreGive(s_rx_lock);
}

static int64_t open_request(unsigned slot, const char *path) {
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    uintptr_t token = (uintptr_t)++s_rx[slot].generation << 1 | slot;
    s_rx[slot].length = 0;
    s_rx[slot].overflow = s_rx[slot].done = false;
    s_rx[slot].status = 0;
    xSemaphoreGive(s_rx_lock);
    const char *headers[] = {"Content-Type", "application/json", "Accept",
                             slot ? "application/x-ndjson" : "application/json", NULL};
    return noise_ctrl_req_open("POST", path, headers, false, on_frame, (void *)token);
}

static void close_requests(int64_t post, int64_t sub) {
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    s_rx[0].generation++;
    s_rx[1].generation++;
    xSemaphoreGive(s_rx_lock);
    if (post) noise_ctrl_req_cancel(post);
    if (sub) noise_ctrl_req_cancel(sub);
}

// One turn subscribes before posting; accepted turns are drained without reposting.
// 每轮先订阅再发送；已受理的消息持续读取回复并保持单次发送。
static bool send_event(event_t *event) {
    if (!noise_ctrl_is_connected()) return false;
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    memset(s_candidates, 0, 4 * sizeof(*s_candidates));
    s_candidate_next = 0;
    s_note_id[0] = s_parent_id[0] = s_echo_id[0] = s_rx_reply[0] = 0;
    s_replied = false;
    xSemaphoreGive(s_rx_lock);
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "message", event->message);
    cJSON_AddStringToObject(body, "output_modality", "text");
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return false;
    int64_t sub = open_request(1, "/chat/subscribe"), post = 0;
    bool ok = sub && noise_ctrl_req_send(sub, "{}", 2, true, 1000);
    if (ok) post = open_request(0, "/chat/stream");
    bool submitted = ok && post;
    size_t length = strlen(json);
    // Queue small body chunks within every board's daemon-request frame bound.
    // 分段提交请求正文，使每段均处于各板卡的请求帧上限内。
    for (size_t offset = 0; submitted && offset < length; offset += 1024) {
        size_t n = length - offset < 1024 ? length - offset : 1024;
        submitted = noise_ctrl_req_send(post, json + offset, n, offset + n == length, 1000);
    }
    cJSON_free(json);
    if (!submitted) { close_requests(post, sub); return false; }
    int64_t deadline = esp_timer_get_time() + REPLY_TIMEOUT_US;
    bool announced = false, replied = false;
    int status = 0;
    char reply[GADGET_EVENTS_REPLY_BYTES] = {0};
    for (;;) {
        xSemaphoreTake(s_rx_lock, portMAX_DELAY);
        bool ack = s_note_id[0] && s_rx[0].status == 200;
        status = s_rx[0].status;
        replied = s_replied;
        bool settled = replied && esp_timer_get_time() - s_reply_at >= REPLY_SETTLE_US;
        bool failed = (s_rx[0].done && !ack) || (s_rx[1].done && !replied);
        if (settled || failed || esp_timer_get_time() >= deadline) {
            utf8_copy(reply, sizeof(reply), s_rx_reply, false);
            xSemaphoreGive(s_rx_lock);
            if (ack && !announced) progress(event, "sent", status, NULL);
            break;
        }
        xSemaphoreGive(s_rx_lock);
        if (ack && !announced) { progress(event, "sent", status, NULL); announced = true; }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    close_requests(post, sub);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    utf8_copy(s_reply, sizeof(s_reply), reply, false);
    xSemaphoreGive(s_lock);
    if (replied) progress(event, "reply", status, reply);
    else progress(event, "error", status, "reply unavailable or timed out; message may have been delivered");
    return true;
}

static void event_task(void *arg) {
    (void)arg;
    for (;;) {
        if (!noise_ctrl_is_connected()) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        event_t *event = dequeue();
        if (!event) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); continue; }
        unsigned backoff = 1000;
        while (!send_event(event)) {
            vTaskDelay(pdMS_TO_TICKS(backoff));
            if (backoff < 30000) backoff = backoff > 15000 ? 30000 : backoff * 2;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        release(event);
        xSemaphoreGive(s_lock);
    }
}

bool gadget_events_init(void) {
    if (atomic_load(&s_ready)) return true;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_rx_lock) s_rx_lock = xSemaphoreCreateMutex();
    if (!s_lock || !s_rx_lock) return false;
    // Allocate receive rows once in PSRAM; internal RAM is used without PSRAM.
    // 接收行仅分配一次；有 PSRAM 时使用 PSRAM，否则使用内部内存。
    int caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#if CONFIG_SPIRAM
    if (esp_psram_is_initialized()) caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#endif
    if (!s_rx) s_rx = heap_caps_calloc(2, sizeof(*s_rx), caps);
    if (!s_candidates) s_candidates = heap_caps_calloc(4, sizeof(*s_candidates), caps);
    if (!s_rx || !s_candidates) return false;
    if (xTaskCreate(event_task, "gadget_events", 6144, NULL, 3, &s_task) != pdPASS) return false;
    atomic_store(&s_ready, true);
    return true;
}
