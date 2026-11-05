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
#define REPLY_SETTLE_US (3LL * 1000000)
#define SILENT_SETTLE_US (4LL * 1000000)
#define BUSY_HOLD_US (20LL * 1000000)
#define REPLY_MESSAGES 8
#define REPLY_ECHOES 4
#define REPLY_RECENT_SEQS 64
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
static atomic_bool s_ready, s_debug;
static event_t *s_queue[GADGET_EVENTS_PENDING], *s_history[GADGET_EVENTS_HISTORY];
static unsigned s_head, s_count, s_history_head, s_history_count;
static uint64_t s_last_seq, s_dropped;
static char s_reply[GADGET_EVENTS_REPLY_BYTES];

static int memory_caps(void) {
#if CONFIG_SPIRAM
    if (esp_psram_is_initialized()) return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#endif
    return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
}

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
    event_t *event = heap_caps_calloc(1, sizeof(*event) + ns + nt + nd + message_cap, memory_caps());
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

static bool enqueue(const char *source, const char *summary, const cJSON *data, bool console, uint64_t *seq) {
    if (!atomic_load(&s_ready)) return false;
    event_t *event = make_event(source, summary, data);
    if (!event) return false;
    if (xSemaphoreTake(s_lock, 0) != pdTRUE) { free(event); return false; }
    if (s_last_seq == MAX_SEQ) { xSemaphoreGive(s_lock); free(event); return false; }
    event->seq = ++s_last_seq;
    if (seq) *seq = event->seq;
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
    return enqueue(source, summary, data, false, NULL);
}

bool gadget_events_post_seq(const char *source, const char *summary, const cJSON *data, uint64_t *seq) {
    return enqueue(source, summary, data, false, seq);
}

static event_t *dequeue(void) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    event_t *event = s_count ? s_queue[s_head] : NULL;
    if (event) { s_head = (s_head + 1) % GADGET_EVENTS_PENDING; s_count--; }
    xSemaphoreGive(s_lock);
    return event;
}

static bool events_pending(void) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool pending = s_count != 0;
    xSemaphoreGive(s_lock);
    return pending;
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
    if (!line) return false;
    if (!strcmp(line, "event.debug=1") || !strcmp(line, "event.debug=0")) {
        bool enabled = line[12] == '1';
        atomic_store(&s_debug, enabled);
        flockfile(stdout);
        printf("@event.debug {\"enabled\":%s}\n", enabled ? "true" : "false");
        fflush(stdout);
        funlockfile(stdout);
        return true;
    }
    if (strncmp(line, "event=", 6)) return false;
    if (!enqueue("console", line + 6, NULL, true, NULL)) {
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
    size_t length, debug_length;
    size_t frame_bytes, frame_newlines;
    int64_t frame_next_at;
    char row[ROW_MAX + 1];
} event_rx_t;
static event_rx_t *s_rx;
typedef struct {
    char id[96], session[96], text[GADGET_EVENTS_REPLY_BYTES];
    uint64_t started;
    bool done;
} event_candidate_t;
typedef struct {
    char id[96], session[96];
    uint64_t row;
    bool idle;
} event_echo_t;
typedef struct {
    event_candidate_t messages[REPLY_MESSAGES];
    event_echo_t echoes[REPLY_ECHOES];
    unsigned count, echo_next, seq_next;
    uint64_t row, echo_row, server_seq;
    uint64_t recent_seqs[REPLY_RECENT_SEQS];
    char note_id[96], session[96], reply[GADGET_EVENTS_REPLY_BYTES];
    int64_t activity_at, content_at;
    bool started, complete, overflow, agent_busy;
} event_reply_t;
static event_reply_t *s_stream;

static const char *field(const cJSON *object, const char *key) {
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, key));
    return s ? s : "";
}

static const cJSON *row_payload(const cJSON *root) {
    const cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    return cJSON_IsObject(payload) ? payload : root;
}

static const char *event_kind(const cJSON *root, const cJSON *payload) {
    const char *kind = field(root, "event_name");
    if (!kind[0]) kind = field(root, "event");
    if (!kind[0]) kind = field(payload, "event_name");
    if (!kind[0]) kind = field(payload, "event");
    return kind;
}

static void debug_json(const char *prefix, cJSON *json) {
    char *line = cJSON_PrintUnformatted(json);
    if (line) {
        flockfile(stdout);
        printf("%s %s\n", prefix, line);
        fflush(stdout);
        funlockfile(stdout);
    }
    cJSON_free(line);
    cJSON_Delete(json);
}

// Collects object keys and string IDs, with paths for nested IDs.
// 收集对象键名及字符串编号，嵌套编号带上所属路径。
static void debug_fields(const cJSON *object, cJSON *keys, cJSON *ids, const char *prefix) {
    if (!cJSON_IsObject(object)) return;
    const cJSON *item;
    cJSON_ArrayForEach(item, object) {
        const char *key = item->string;
        if (keys) cJSON_AddItemToArray(keys, cJSON_CreateString(key));
        size_t length = strlen(key);
        if (length < 2 || strcmp(key + length - 2, "id") || !cJSON_IsString(item)) continue;
        size_t capacity = strlen(prefix) + length + 1;
        char *path = malloc(capacity);
        if (!path) continue;
        snprintf(path, capacity, "%s%s", prefix, key);
        cJSON_AddStringToObject(ids, path, item->valuestring);
        free(path);
    }
}

// Prints row metadata; discriminator values are strings or null.
// 输出行元数据；事件分类字段的值为字符串或空值。
static void debug_row(unsigned slot, int status, size_t length, const cJSON *root, bool dropped) {
    if (!atomic_load(&s_debug)) return;
    cJSON *json = cJSON_CreateObject(), *keys = cJSON_CreateArray();
    cJSON *payload_keys = cJSON_CreateArray(), *ids = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "req", slot ? "sub" : "post");
    cJSON_AddNumberToObject(json, "status", status);
    cJSON_AddNumberToObject(json, "len", (double)length);
    const cJSON *seq = cJSON_GetObjectItemCaseSensitive(root, "seq");
    cJSON_AddItemToObject(json, "seq", cJSON_IsNumber(seq) && isfinite(seq->valuedouble)
                         ? cJSON_CreateNumber(seq->valuedouble) : cJSON_CreateNull());
    const cJSON *payload = row_payload(root);
    const char *kind = event_kind(root, payload);
    bool is_status = !strcmp(kind, "agent.status") || !strcmp(kind, "task.status");
    const char *code = is_status ? cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(payload, "activity_code")) : NULL;
    const char *state = is_status ? cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(payload, "status")) : NULL;
    cJSON_AddItemToObject(json, "activity_code", code ? cJSON_CreateString(code) : cJSON_CreateNull());
    cJSON_AddItemToObject(json, "payload_status", state ? cJSON_CreateString(state) : cJSON_CreateNull());
    if (dropped) cJSON_AddStringToObject(json, "dropped", "seq");
    cJSON_AddItemToObject(json, "keys", keys);
    const char *names[] = {"type", "event", "event_name"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, names[i]));
        cJSON_AddItemToObject(json, names[i], value ? cJSON_CreateString(value) : cJSON_CreateNull());
    }
    cJSON_AddItemToObject(json, "payload_keys", payload_keys);
    cJSON_AddItemToObject(json, "ids", ids);
    debug_fields(root, keys, ids, "");
    debug_fields(cJSON_GetObjectItemCaseSensitive(root, "result"), NULL, ids, "result.");
    debug_fields(cJSON_GetObjectItemCaseSensitive(root, "payload"), payload_keys, ids, "payload.");
    debug_json("@event.row", json);
}

static void debug_request(unsigned slot, int status, const char *phase) {
    if (!atomic_load(&s_debug)) return;
    flockfile(stdout);
    printf("@event.request {\"req\":\"%s\",\"status\":%d,\"phase\":\"%s\"}\n",
           slot ? "sub" : "post", status, phase);
    fflush(stdout);
    funlockfile(stdout);
}

// Prints the terminal matcher state and candidate identifiers, lengths and flags.
// 输出收尾时的匹配状态、候选消息编号、长度和标志。
static void debug_state(const char *reason) {
    if (!atomic_load(&s_debug)) return;
    cJSON *json = cJSON_CreateObject(), *candidates = cJSON_CreateArray();
    cJSON_AddStringToObject(json, "reason", reason);
    cJSON_AddBoolToObject(json, "note_id_set", s_stream->note_id[0] != 0);
    cJSON_AddNumberToObject(json, "echo_row", (double)s_stream->echo_row);
    cJSON_AddNumberToObject(json, "row", (double)s_stream->row);
    cJSON_AddNumberToObject(json, "server_seq", (double)s_stream->server_seq);
    cJSON_AddNumberToObject(json, "count", s_stream->count);
    cJSON_AddItemToObject(json, "candidates", candidates);
    for (unsigned i = 0; i < s_stream->count; i++) {
        const event_candidate_t *message = &s_stream->messages[i];
        cJSON *item = cJSON_CreateObject();
        size_t n = strlen(message->id);
        cJSON_AddStringToObject(item, "id_tail", message->id + (n > 8 ? n - 8 : 0));
        cJSON_AddNumberToObject(item, "started", (double)message->started);
        cJSON_AddBoolToObject(item, "done", message->done);
        cJSON_AddNumberToObject(item, "text_len", strlen(message->text));
        cJSON_AddItemToArray(candidates, item);
    }
    cJSON_AddBoolToObject(json, "complete", s_stream->complete);
    cJSON_AddBoolToObject(json, "agent_busy", s_stream->agent_busy);
    cJSON_AddNumberToObject(json, "pending", s_rx[1].length);
    cJSON_AddBoolToObject(json, "overflow", s_rx[1].overflow || s_stream->overflow);
    int64_t now = esp_timer_get_time();
    cJSON_AddNumberToObject(json, "quiet_ms", (now - s_stream->activity_at) / 1000);
    cJSON_AddNumberToObject(json, "content_age_ms", (now - s_stream->content_at) / 1000);
    debug_json("@event.state", json);
}

static bool row_space(uint8_t byte) {
    return byte == ' ' || byte == '\r' || byte == '\t';
}

static bool pending_content(const event_rx_t *rx) {
    for (size_t i = 0; i < rx->length; i++) {
        if (!row_space((uint8_t)rx->row[i])) return true;
    }
    return false;
}

// Aggregates subscribe frame counts at most once per second.
// 汇总订阅数据帧计数，每秒最多输出一次。
static void debug_frame(size_t bytes, size_t newlines) {
    event_rx_t *rx = &s_rx[1];
    if (!atomic_load(&s_debug)) {
        rx->frame_bytes = rx->frame_newlines = 0;
        rx->frame_next_at = 0;
        return;
    }
    if (!bytes) return;
    rx->frame_bytes += bytes;
    rx->frame_newlines += newlines;
    int64_t now = esp_timer_get_time();
    if (now < rx->frame_next_at) return;
    flockfile(stdout);
    printf("@event.frame {\"req\":\"sub\",\"bytes\":%zu,\"newlines\":%zu,\"pending\":%zu,\"ws_only\":%s}\n",
           rx->frame_bytes, rx->frame_newlines, rx->length,
           !rx->overflow && !pending_content(rx) ? "true" : "false");
    fflush(stdout);
    funlockfile(stdout);
    rx->frame_bytes = rx->frame_newlines = 0;
    rx->frame_next_at = now + 1000000;
}


// Matches the acknowledged user echo and joins later assistant messages in start order.
// 匹配已确认的用户回声，并按开始顺序拼接其后的助手消息。
static void collect_reply(void) {
    if (!s_stream->note_id[0] || !s_stream->session[0]) return;
    if (!s_stream->echo_row) {
        for (unsigned i = 0; i < REPLY_ECHOES; i++) {
            event_echo_t *echo = &s_stream->echoes[i];
            if (!strcmp(echo->id, s_stream->note_id) && !strcmp(echo->session, s_stream->session)) {
                s_stream->echo_row = echo->row;
                break;
            }
        }
    }
    s_stream->complete = false;
    s_stream->started = false;
    s_stream->reply[0] = 0;
    if (!s_stream->echo_row) return;
    unsigned found = 0;
    bool done = !s_stream->overflow;
    for (unsigned i = 0; i < s_stream->count; i++) {
        event_candidate_t *message = &s_stream->messages[i];
        if (message->started <= s_stream->echo_row || strcmp(message->session, s_stream->session)) continue;
        found++;
        done = done && message->done;
        if (message->text[0]) {
            if (s_stream->reply[0]) utf8_copy(s_stream->reply, sizeof(s_stream->reply), "\n", true);
            utf8_copy(s_stream->reply, sizeof(s_stream->reply), message->text, true);
        }
    }
    s_stream->started = found != 0;
    s_stream->complete = found && done;
}

// Settles quiet completed replies after idle or a bounded busy hold since content.
// 已完成的回复在静默后收尾，忙碌等待以最后内容行计时并设有上限。
static bool reply_settled(void) {
    int64_t now = esp_timer_get_time();
    return s_stream->complete
        && (!s_stream->agent_busy || now - s_stream->content_at >= BUSY_HOLD_US)
        && !pending_content(&s_rx[1]) && !s_rx[1].overflow
        && now - s_stream->activity_at >= REPLY_SETTLE_US;
}

// Settles an echo followed by idle status and quiet time without an assistant start.
// 用户回声之后收到空闲状态，且没有助手消息开始时，在静默后收尾。
static bool silent_settled(void) {
    if (!s_stream->echo_row || s_stream->started || s_stream->overflow
        || pending_content(&s_rx[1]) || s_rx[1].overflow
        || esp_timer_get_time() - s_stream->activity_at < SILENT_SETTLE_US) return false;
    for (unsigned i = 0; i < REPLY_ECHOES; i++) {
        if (s_stream->echoes[i].row == s_stream->echo_row) return s_stream->echoes[i].idle;
    }
    return false;
}

// Remembers recent sequence values and accepts unseen values in any arrival order.
// 记录近期序号，并按到达顺序接收尚未出现过的序号。
static bool duplicate_seq(const cJSON *root) {
    const cJSON *seq = cJSON_GetObjectItemCaseSensitive(root, "seq");
    if (!cJSON_IsNumber(seq) || !isfinite(seq->valuedouble) || seq->valuedouble <= 0
        || seq->valuedouble > MAX_SEQ || floor(seq->valuedouble) != seq->valuedouble) return false;
    uint64_t value = (uint64_t)seq->valuedouble;
    for (unsigned i = 0; i < REPLY_RECENT_SEQS; i++) {
        if (s_stream->recent_seqs[i] == value) return true;
    }
    s_stream->recent_seqs[s_stream->seq_next++ % REPLY_RECENT_SEQS] = value;
    if (value > s_stream->server_seq) s_stream->server_seq = value;
    return false;
}

// Records starts independently of acknowledgement arrival, preserving echo ordering.
// 独立记录消息开始顺序，以便在确认稍晚到达时保留回声边界。
static void parse_row(const char *row, size_t length, unsigned slot) {
    cJSON *root = cJSON_ParseWithLength(row, length);
    bool dropped = slot && s_rx[slot].status == 200
        && !strcmp(field(root, "type"), "event") && duplicate_seq(root);
    debug_row(slot, s_rx[slot].status, length - 1, root, dropped);
    if (!root || s_rx[slot].status != 200 || dropped) { cJSON_Delete(root); return; }
    if (!slot) {
        cJSON *ack = cJSON_GetObjectItemCaseSensitive(root, "result");
        if (!cJSON_IsObject(ack)) ack = root;
        const char *id = field(ack, "message_id"), *session = field(ack, "session_id");
        if (id[0] && session[0] && strlen(id) < sizeof(s_stream->note_id) && strlen(session) < sizeof(s_stream->session)) {
            utf8_copy(s_stream->note_id, sizeof(s_stream->note_id), id, false);
            utf8_copy(s_stream->session, sizeof(s_stream->session), session, false);
            unsigned kept = 0;
            for (unsigned i = 0; i < s_stream->count; i++) {
                if (!strcmp(s_stream->messages[i].session, session)) s_stream->messages[kept++] = s_stream->messages[i];
            }
            s_stream->count = kept;
            collect_reply();
        }
        cJSON_Delete(root);
        return;
    }
    if (strcmp(field(root, "type"), "event")) { cJSON_Delete(root); return; }
    uint64_t order = ++s_stream->row;
    const cJSON *payload = row_payload(root);
    const char *kind = event_kind(root, payload);
    const char *session = field(payload, "session_id"), *id = field(payload, "message_id");
    if (!id[0]) id = field(payload, "id");
    // Parsed events set quiet time; the acknowledgement selects the session to follow.
    // 已解析事件确定静默计时，发送确认选定后续跟踪的会话。
    bool ours = !session[0] || !strcmp(session, s_stream->session);
    if (ours || !s_stream->session[0]) s_stream->activity_at = esp_timer_get_time();
    bool agent_status = !strcmp(kind, "agent.status"), task_status = !strcmp(kind, "task.status");
    if (agent_status || task_status) {
        const char *code = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(payload, "activity_code"));
        const char *status = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(payload, "status"));
        const char *state = agent_status ? (code ? code : status) : (status ? status : code);
        if (state) {
            bool idle = agent_status ? (!strcmp(state, "online") || !strcmp(state, "idle"))
                                     : (!strcmp(state, "completed") || !strcmp(state, "failed"));
            if (ours) s_stream->agent_busy = state[0] && !idle;
            // Echo-local status also covers an acknowledgement arriving after idle.
            // 每个回声保存自己的状态，也覆盖发送确认晚于空闲状态到达的情况。
            for (unsigned i = 0; i < REPLY_ECHOES; i++) {
                event_echo_t *echo = &s_stream->echoes[i];
                if (echo->row && order > echo->row && (!session[0] || !strcmp(session, echo->session))) echo->idle = idle;
            }
        }
    }
    if (!strcmp(kind, "message.user") && id[0] && session[0]) {
        if (!s_stream->echo_row && strlen(id) < 96 && strlen(session) < 96) {
            event_echo_t *echo = &s_stream->echoes[s_stream->echo_next++ % REPLY_ECHOES];
            utf8_copy(echo->id, sizeof(echo->id), id, false);
            utf8_copy(echo->session, sizeof(echo->session), session, false);
            echo->row = order;
            echo->idle = false;
        }
    } else {
        bool start = !strcmp(kind, "delta.message_start");
        bool append = !strcmp(kind, "delta.text_append");
        bool done = !strcmp(kind, "delta.message_done");
        bool full = !strcmp(kind, "message.assistant");
        if (id[0] && session[0] && (!s_stream->session[0] || !strcmp(session, s_stream->session))
            && (start || append || done || full) && strlen(id) < 96 && strlen(session) < 96) {
            unsigned i = 0;
            while (i < s_stream->count && (strcmp(s_stream->messages[i].id, id) || strcmp(s_stream->messages[i].session, session))) i++;
            if (i == s_stream->count && (start || full)) {
                if (i < REPLY_MESSAGES) {
                    event_candidate_t *message = &s_stream->messages[s_stream->count++];
                    memset(message, 0, sizeof(*message));
                    utf8_copy(message->id, sizeof(message->id), id, false);
                    utf8_copy(message->session, sizeof(message->session), session, false);
                    message->started = order;
                } else if (s_stream->echo_row && order > s_stream->echo_row) s_stream->overflow = true;
            }
            if (i < s_stream->count) {
                s_stream->content_at = esp_timer_get_time();
                event_candidate_t *message = &s_stream->messages[i];
                const char *text = field(payload, "display_text");
                if (!text[0]) text = field(payload, "content");
                if (!text[0]) text = field(payload, "text");
                if (text[0]) utf8_copy(message->text, sizeof(message->text), text, append);
                if (done || (full && !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(payload, "display_text_ready")))) message->done = true;
            }
        }
    }
    collect_reply();
    cJSON_Delete(root);
}

// Skips leading whitespace, joins bounded rows across frames and drains oversized rows.
// 跳过行首空白、跨帧拼接有界数据行，并排空超长行。
static void on_frame(void *ctx, int status, const uint8_t *data, size_t len, bool end) {
    uintptr_t token = (uintptr_t)ctx;
    unsigned slot = token & 1;
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    if ((uint32_t)(token >> 1) == s_rx[slot].generation && !s_rx[slot].done) {
        if (status) {
            s_rx[slot].status = status;
            debug_request(slot, status, "status");
        }
        size_t newlines = 0;
        if (s_rx[slot].status == 200 || atomic_load(&s_debug)) {
            for (size_t i = 0; i < len; i++) {
                if (data[i] == '\n') {
                    newlines++;
                    if (!s_rx[slot].overflow && s_rx[slot].length) {
                        s_rx[slot].row[s_rx[slot].length] = 0;
                        parse_row(s_rx[slot].row, s_rx[slot].length + 1, slot);
                    } else if (s_rx[slot].overflow) debug_row(slot, s_rx[slot].status, s_rx[slot].debug_length, NULL, false);
                    s_rx[slot].length = s_rx[slot].debug_length = 0;
                    s_rx[slot].overflow = false;
                } else {
                    if (!s_rx[slot].length && !s_rx[slot].overflow && row_space(data[i])) continue;
                    s_rx[slot].debug_length++;
                    if (s_rx[slot].length < ROW_MAX && !s_rx[slot].overflow) s_rx[slot].row[s_rx[slot].length++] = (char)data[i];
                    else s_rx[slot].overflow = true;
                }
            }
            if (end && s_rx[slot].debug_length) {
                if (!s_rx[slot].overflow && s_rx[slot].length) {
                    s_rx[slot].row[s_rx[slot].length] = 0;
                    parse_row(s_rx[slot].row, s_rx[slot].length + 1, slot);
                } else if (s_rx[slot].overflow) debug_row(slot, s_rx[slot].status, s_rx[slot].debug_length, NULL, false);
                s_rx[slot].length = s_rx[slot].debug_length = 0;
            }
        }
        if (slot) debug_frame(len, newlines);
        s_rx[slot].done = end || status < 0;
        if (s_rx[slot].done) debug_request(slot, s_rx[slot].status, "end");
    }
    xSemaphoreGive(s_rx_lock);
}

static int64_t open_request(unsigned slot, const char *path) {
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    uintptr_t token = (uintptr_t)++s_rx[slot].generation << 1 | slot;
    s_rx[slot].length = s_rx[slot].debug_length = 0;
    s_rx[slot].frame_bytes = s_rx[slot].frame_newlines = 0;
    s_rx[slot].frame_next_at = 0;
    s_rx[slot].overflow = s_rx[slot].done = false;
    s_rx[slot].status = 0;
    xSemaphoreGive(s_rx_lock);
    const char *headers[] = {"Content-Type", "application/json", "Accept",
                             slot ? "application/x-ndjson" : "application/json", NULL};
    return noise_ctrl_req_open("POST", path, headers, false, on_frame, (void *)token);
}

static void close_requests(int64_t post, int64_t sub) {
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    if (post) debug_request(0, s_rx[0].status, "close");
    if (sub) debug_request(1, s_rx[1].status, "close");
    s_rx[0].generation++;
    s_rx[1].generation++;
    xSemaphoreGive(s_rx_lock);
    if (post) noise_ctrl_req_cancel(post);
    if (sub) noise_ctrl_req_cancel(sub);
}

// Each turn subscribes before posting; an echo releases queued posts in FIFO order.
// 每轮先订阅再发送；收到回声后，待发事件按先入先出顺序继续发送。
static bool send_event(event_t *event) {
    if (!noise_ctrl_is_connected()) return false;
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    memset(s_stream, 0, sizeof(*s_stream));
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
    if (!submitted) {
        xSemaphoreTake(s_rx_lock, portMAX_DELAY);
        debug_state("failed");
        xSemaphoreGive(s_rx_lock);
        close_requests(post, sub);
        return false;
    }
    int64_t deadline = esp_timer_get_time() + REPLY_TIMEOUT_US;
    bool announced = false, replied = false, delivered = false;
    int status = 0;
    char reply[GADGET_EVENTS_REPLY_BYTES] = {0};
    for (;;) {
        bool pending = events_pending();
        xSemaphoreTake(s_rx_lock, portMAX_DELAY);
        bool ack = s_stream->note_id[0] && s_rx[0].status == 200;
        status = s_rx[0].status;
        replied = s_stream->complete;
        bool settled = reply_settled();
        bool silent = ack && silent_settled();
        bool next = ack && s_stream->echo_row && pending;
        bool failed = (s_rx[0].done && !ack) || (s_rx[1].done && !replied);
        if (settled || silent || next || failed || esp_timer_get_time() >= deadline) {
            debug_state(settled ? "settled" : silent ? "silent" : next ? "next" : failed ? "failed" : "timeout");
            replied = settled;
            delivered = silent || next;
            if (!delivered || replied) utf8_copy(reply, sizeof(reply), s_stream->reply, false);
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
    else if (delivered) progress(event, "delivered", 0, NULL);
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
    int caps = memory_caps();
    if (!s_rx) s_rx = heap_caps_calloc(2, sizeof(*s_rx), caps);
    if (!s_stream) s_stream = heap_caps_calloc(1, sizeof(*s_stream), caps);
    if (!s_rx || !s_stream) return false;
#if CONFIG_SPIRAM
    if (caps & MALLOC_CAP_SPIRAM) {
        if (xTaskCreateWithCaps(event_task, "gadget_events", 6144, NULL, 3, &s_task,
                                MALLOC_CAP_SPIRAM) != pdPASS) return false;
    } else
#endif
    if (xTaskCreate(event_task, "gadget_events", 6144, NULL, 3, &s_task) != pdPASS) return false;
    atomic_store(&s_ready, true);
    return true;
}
