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
    frame(0, "{\"result\":{\"message_id\":\"n1\",\"reply_to_message_id\":\"n1\",\"session_id\":\"care-session\"}}", true);
    if (!no_reply) {
        frame(1, "{\"type\":\"event\",\"event\":\"message.user\",\"payload\":{\"message_id\":\"n1\",\"session_id\":\"care-session\"}}\n", false);
        frame(1, "{\"type\":\"event\",\"event\":\"delta.message_start\",\"payload\":{\"message_id\":\"assistant-msg-turn\",\"reply_to_message_id\":\"\",\"session_id\":\"care-session\"}}\n", false);
        frame(1, "{\"type\":\"event\",\"event\":\"delta.text_append\",\"payload\":{\"message_id\":\"assistant-msg-turn\",\"parent_message_id\":\"assistant-msg-turn\",\"text\":\"All ready!\",\"session_id\":\"care-session\"}}\n", false);
        frame(1, "{\"type\":\"event\",\"event\":\"delta.message_done\",\"payload\":{\"message_id\":\"assistant-msg-turn\",\"session_id\":\"care-session\"}}\n", false);
    }
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
    if (s_stream) memset(s_stream,0,sizeof(*s_stream));
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
    frame(1, "{\"type\":\"event\",\"event_name\":\"message.assistant\",\"payload\":{\"message_id\":\"a2\",\"session_id\":\"care-session\",\"text\":\"late\"}}\n", false);
    assert(!strcmp(s_stream->reply, "All ready!"));
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
    uint64_t seq=0;assert(gadget_events_post_seq("test","sequence",NULL,&seq)&&seq==1);
    fail_lock=true;assert(!gadget_events_post_seq("test","contended",NULL,&seq)&&seq==1);
    reset();
}

static void event_row(const char *kind,const char *id,const char *session,const char *text) {
    cJSON *row=cJSON_CreateObject(),*p=cJSON_CreateObject();
    cJSON_AddStringToObject(row,"type","event");cJSON_AddStringToObject(row,"event",kind);cJSON_AddItemToObject(row,"payload",p);
    cJSON_AddStringToObject(p,"message_id",id);cJSON_AddStringToObject(p,"session_id",session);
    cJSON_AddStringToObject(p,"reply_to_message_id","");cJSON_AddStringToObject(p,"parent_message_id",id);
    if(text)cJSON_AddStringToObject(p,!strcmp(kind,"message.assistant")?"display_text":"text",text);
    char *json=cJSON_PrintUnformatted(row);assert(json);frame(1,json,false);frame(1,"\n",false);
    cJSON_free(json);cJSON_Delete(row);
}
static void ack_row(void) {frame(0,"{\"message_id\":\"my-note\",\"reply_to_message_id\":\"my-note\",\"session_id\":\"care-session\"}",true);}
static void begin_parse(void) {
    memset(s_stream,0,sizeof(*s_stream));open_request(1,"/chat/subscribe");open_request(0,"/chat/stream");
}
static void parser_tests(void) {
    begin_parse();ack_row();
    event_row("delta.message_start","old","care-session",NULL);
    event_row("message.user","other-note","care-session",NULL);
    event_row("message.assistant","unrelated","other-session","Ignore me");
    assert(!s_stream->complete && !s_stream->echo_row);
    event_row("message.user","my-note","care-session",NULL);
    event_row("delta.text_append","old","care-session","Old turn");
    event_row("delta.message_done","old","care-session",NULL);
    assert(!s_stream->complete);
    event_row("delta.message_start","a","care-session",NULL);
    event_row("delta.text_append","a","care-session","Hello ");
    event_row("delta.message_start","b","care-session",NULL);
    event_row("delta.text_append","b","care-session","Second reply");
    event_row("delta.text_append","a","care-session","Muse");
    event_row("delta.message_done","a","care-session",NULL);
    now+=4000000;assert(!reply_settled());
    event_row("delta.message_done","b","care-session",NULL);
    assert(s_stream->complete && !strcmp(s_stream->reply,"Hello Muse\nSecond reply"));
    now+=REPLY_SETTLE_US-1;assert(!reply_settled());
    event_row("agent.status","unrelated-turn-id","care-session",NULL);
    assert(!reply_settled());now+=REPLY_SETTLE_US;assert(reply_settled());
    event_row("delta.message_start","c","care-session",NULL);assert(!reply_settled());
    event_row("delta.text_append","c","care-session","Third reply");
    event_row("delta.message_done","c","care-session",NULL);
    assert(!strcmp(s_stream->reply,"Hello Muse\nSecond reply\nThird reply"));
    close_requests(1,2);

    // An echo can precede the acknowledgement on the independent request stream.
    // 独立请求流上的用户回声可以先于发送确认到达。
    begin_parse();event_row("message.user","my-note","care-session",NULL);
    event_row("message.assistant","foreign","other-session","PRIVATE_OTHER");
    event_row("delta.message_start","a","care-session",NULL);
    event_row("delta.text_append","a","care-session","Early echo reply");
    event_row("delta.message_done","a","care-session",NULL);assert(!s_stream->complete);
    ack_row();assert(s_stream->complete && !strcmp(s_stream->reply,"Early echo reply"));
    close_requests(1,2);

    begin_parse();event_row("message.assistant","foreign","other-session","PRIVATE_OTHER");ack_row();
    event_row("message.user","my-note","care-session",NULL);
    for(unsigned i=0;i<REPLY_MESSAGES+2;i++)event_row("message.assistant","foreign","other-session","PRIVATE_OTHER");
    event_row("delta.message_start","a","care-session",NULL);assert(!s_stream->complete);
    event_row("delta.text_append","a","care-session","Own session");event_row("delta.message_done","a","care-session",NULL);
    assert(!s_stream->overflow && s_stream->complete && !strcmp(s_stream->reply,"Own session"));close_requests(1,2);

    begin_parse();ack_row();event_row("message.user","my-note","care-session",NULL);
    char *big=malloc(56324);assert(big);memset(big,'x',56323);big[56323]=0;frame(1,big,false);free(big);frame(1,"\n",false);
    event_row("message.assistant","after-overflow","care-session","After overflow");
    assert(s_stream->complete && !strcmp(s_stream->reply,"After overflow"));
    char many[1000],clipped[601];
    for(int i=0;i<333;i++)memcpy(many+3*i,"\xE4\xB8\xAD",3);many[999]=0;
    utf8_copy(clipped,sizeof(clipped),many,false);
    assert(strlen(clipped)==600 && !memcmp(clipped+597,"\xE4\xB8\xAD",3));
    event_row("message.assistant","long","care-session",many);assert(strlen(s_stream->reply)==600);
    close_requests(1,2);

    begin_parse();ack_row();event_row("message.user","my-note","care-session",NULL);
    for(unsigned i=0;i<REPLY_MESSAGES+1;i++){char id[12];snprintf(id,sizeof(id),"many-%u",i);event_row("message.assistant",id,"care-session","reply");}
    now+=REPLY_SETTLE_US;assert(s_stream->overflow&&!reply_settled());close_requests(1,2);
}

static void recorded_stream_tests(void) {
    begin_parse();FILE *file=fopen(REPLY_FIXTURE_PATH,"rb");assert(file);
    assert(!fseek(file,0,SEEK_END));long size=ftell(file);assert(size>0);rewind(file);
    char *data=calloc((size_t)size+1,1);assert(data&&fread(data,1,(size_t)size,file)==(size_t)size);fclose(file);
    cJSON *fixture=cJSON_Parse(data);free(data);assert(fixture);
    cJSON *entry;cJSON_ArrayForEach(entry,cJSON_GetObjectItem(fixture,"rows")) {
        unsigned slot=!strcmp(field(entry,"req"),"sub");
        cJSON *oversized=cJSON_GetObjectItem(entry,"oversized");
        if(oversized){size_t n=(size_t)oversized->valueint;char *big=calloc(n+1,1);assert(big);memset(big,'x',n);frame(slot,big,false);frame(slot,"\n",false);free(big);continue;}
        char *json=cJSON_PrintUnformatted(cJSON_GetObjectItem(entry,"row"));assert(json);size_t n=strlen(json),split=n/3;
        callbacks[slot](contexts[slot],200,(const uint8_t *)json,split,false);
        callbacks[slot](contexts[slot],0,(const uint8_t *)json+split,n-split,!slot);
        if(slot)frame(slot,"\n",false);cJSON_free(json);
    }
    cJSON_Delete(fixture);assert(s_stream->complete&&!strcmp(s_stream->reply,"All ready!"));
    now+=REPLY_SETTLE_US-1;assert(!reply_settled());now++;assert(reply_settled());close_requests(1,2);
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
    assert(gadget_events_post("test","no echo",NULL));
    event_t *unanswered=dequeue();no_reply=true;int64_t before=now;
    assert(send_event(unanswered)&&opens==2&&now-before>=REPLY_TIMEOUT_US);release(unanswered);reset();
    assert(gadget_events_console_command("event=Console ready"));
    event_t *event = dequeue(); assert(event);
    assert(send_event(event)); release(event);
    assert(!gadget_events_console_command("other=x"));
    reset();
}

static void debug_tests(void) {
    assert(!atomic_load(&s_debug));
    assert(!gadget_events_console_command("event.debug=2"));
    assert(gadget_events_console_command("event.debug=1"));
    int64_t sub = open_request(1, "/chat/subscribe"), post = open_request(0, "/chat/stream");
    const char *row = "{\"type\":\"event\",\"event\":\"diag.event\",\"event_name\":\"diag.name\","
        "\"id\":\"top-id\",\"request_id\":\"request-id\",\"rapid\":\"suffix-id\",\"numeric_id\":42,"
        "\"result\":{\"id\":\"result-id\",\"text\":\"PRIVATE_RESULT\"},"
        "\"payload\":{\"id\":\"payload-id\",\"message_id\":\"message-id\",\"reply_to_message_id\":\"parent-id\","
        "\"number_id\":9,\"bool_id\":true,\"text\":\"PRIVATE_TEXT\",\"secret\":\"PRIVATE_SECRET\","
        "\"nested\":{\"id\":\"PRIVATE_NESTED\"}},\"message\":\"PRIVATE_MESSAGE\"}\n";
    size_t split = strlen(row) / 2;
    callbacks[1](contexts[1], 200, (const uint8_t *)row, split, false);
    callbacks[1](contexts[1], 0, (const uint8_t *)row + split, strlen(row) - split, false);
    frame(0, "{\"result\":{\"message_id\":\"post-id\",\"session_id\":\"debug-session\"},\"text\":\"PRIVATE_POST\"}", true);
    frame(1, "{\"type\":{\"text\":\"PRIVATE_TYPE\"},\"event\":[\"PRIVATE_EVENT\"],"
          "\"event_name\":42,\"payload\":{\"text\":\"PRIVATE_PAYLOAD\"}}\nnot-json\n", false);
    char *big = malloc(ROW_MAX + 10); assert(big);
    memset(big, 'x', ROW_MAX + 9); big[ROW_MAX + 9] = 0;
    frame(1, big, false); free(big);
    frame(1, "\n", false);
    callbacks[1](contexts[1], -1, NULL, 0, true);
    close_requests(post, sub);
    frame(1, "{\"id\":\"PRIVATE_LATE\"}\n", false);
    post = open_request(0, "/chat/stream");
    row = "{\"id\":\"error-id\",\"message\":\"PRIVATE_ERROR\"}\n";
    callbacks[0](contexts[0], 503, (const uint8_t *)row, strlen(row), true);
    assert(!strcmp(s_stream->note_id, "post-id"));
    close_requests(post, 0);
    assert(gadget_events_console_command("event.debug=0"));
    post = open_request(0, "/chat/stream");
    frame(0, "{\"id\":\"PRIVATE_DISABLED\"}\n", true);
    close_requests(post, 0);
    assert(!atomic_load(&s_debug));
}


int main(void) {
    queue_tests(); parser_tests(); recorded_stream_tests(); worker_tests(); debug_tests(); reset();
    puts("ok");
    return 0;
}
