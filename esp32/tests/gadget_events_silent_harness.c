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

#define main reply_harness_main
#include "gadget_events_harness.c"
#undef main

static unsigned posted;
static int64_t posted_at[3], began_at;
static char posted_text[3][128];
static unsigned scenario;
static bool echoed, queued;
static noise_ctrl_req_cb old_callback;
static void *old_context;

static void silent_fixture(void) {
    FILE *file = fopen(SILENT_FIXTURE_PATH, "rb"); assert(file);
    char data[4096]; size_t n = fread(data, 1, sizeof(data) - 1, file);
    assert(feof(file)); fclose(file); data[n] = 0;
    cJSON *fixture = cJSON_Parse(data); assert(fixture);
    cJSON *entry;
    cJSON_ArrayForEach(entry, cJSON_GetObjectItem(fixture, "rows")) {
        unsigned slot = !strcmp(field(entry, "req"), "sub");
        char *json = cJSON_PrintUnformatted(cJSON_GetObjectItem(entry, "row")); assert(json);
        frame(slot, json, !slot); if (slot) frame(slot, "\n", false);
        cJSON_free(json);
    }
    cJSON_Delete(fixture);
}

static void silent_parser_tests(void) {
    begin_parse(); silent_fixture();
    assert(s_stream->echo_row && !s_stream->started && !s_stream->complete);
    int64_t last = s_stream->activity_at;
    now += SILENT_SETTLE_US - 1; frame(1, " \r\t", false); assert(!silent_settled());
    now++; assert(silent_settled() && now - last == 4000000);
    status_row("agent.status", "foreign-session", "activity_code", "working");
    assert(silent_settled());
    frame(1, "{", false); assert(!silent_settled()); frame(1, "}\n", false);
    assert(silent_settled());
    status_row("agent.status", "care-session", "activity_code", "working");
    now += REPLY_TIMEOUT_US; assert(!silent_settled());
    status_row("agent.status", "care-session", "activity_code", "idle");
    now += SILENT_SETTLE_US; assert(silent_settled());
    event_row("delta.message_start", "answer", "care-session", NULL);
    now += SILENT_SETTLE_US; assert(!silent_settled());
    event_row("delta.text_append", "answer", "care-session", "Ready");
    event_row("delta.message_done", "answer", "care-session", NULL);
    now += REPLY_SETTLE_US; assert(reply_settled() && !silent_settled());
    close_requests(1, 2);

    begin_parse(); ack_row();
    status_row("agent.status", "care-session", "activity_code", "online");
    status_row("task.status", "care-session", "status", "completed");
    event_row("message.user", "my-note", "care-session", NULL);
    now += SILENT_SETTLE_US; assert(!silent_settled());
    status_row("task.status", "foreign-session", "status", "completed");
    now += SILENT_SETTLE_US; assert(!silent_settled());
    status_row("agent.status", "care-session", "activity_code", "");
    now += SILENT_SETTLE_US; assert(!silent_settled());
    status_row("task.status", NULL, "status", "failed");
    now += SILENT_SETTLE_US; assert(silent_settled()); close_requests(1, 2);

    // Idle after the echo remains valid when the post acknowledgement arrives later.
    // 回声后的空闲状态在发送确认稍晚到达时仍然有效。
    begin_parse(); event_row("message.user", "my-note", "care-session", NULL);
    status_row("agent.status", "care-session", "status", "online");
    now += SILENT_SETTLE_US; assert(!silent_settled());
    ack_row(); assert(silent_settled()); close_requests(1, 2);
}

static bool scripted_post(void) {
    assert(posted < 3);
    posted_at[posted] = now;
    utf8_copy(posted_text[posted], sizeof(posted_text[posted]), sent_message, false);
    posted++;
    if (scenario == 1 && posted == 1) { silent_fixture(); return true; }
    ack_row();
    if (!scenario) {
        event_row("message.user", "my-note", "care-session", NULL);
        status_row("agent.status", "care-session", "activity_code", "working");
        return true;
    }
    if (posted == 1) {
        old_callback = callbacks[1]; old_context = contexts[1];
        if (scenario == 3) {
            event_row("message.user", "my-note", "care-session", NULL);
            event_row("delta.message_start", "slow-answer", "care-session", NULL);
            event_row("delta.text_append", "slow-answer", "care-session", "Partial");
            status_row("agent.status", "care-session", "activity_code", "working");
        }
        return true;
    }
    assert(!s_reply[0]);
    // Late frames from the cancelled request cannot supply the next event's echo.
    // 已取消请求的迟到数据帧不能成为下一条事件的回声。
    if (old_callback) {
        const char *late = "{\"type\":\"event\",\"event\":\"message.user\",\"payload\":{\"message_id\":\"my-note\",\"session_id\":\"care-session\"}}\n";
        old_callback(old_context, 200, (const uint8_t *)late, strlen(late), false);
        assert(!s_stream->echo_row);
    }
    event_row("message.user", "my-note", "care-session", NULL);
    if (scenario == 1 || posted == 3) {
        event_row("message.assistant", "answer", "care-session", "Confirmed.");
    } else {
        event_row("delta.message_start", "slow-answer", "care-session", NULL);
        event_row("delta.text_append", "slow-answer", "care-session", "Partial");
        status_row("agent.status", "care-session", "activity_code", "working");
    }
    return true;
}

static void scripted_delay(void) {
    frame(1, " \r\t", false);
    if (scenario == 1 && !queued && now - began_at >= SILENT_SETTLE_US) {
        queued = true; assert(gadget_events_post("Care Board", "ack", NULL));
    }
    if (scenario == 2 && posted == 1 && !echoed && now - began_at >= 200000) {
        echoed = true; event_row("message.user", "my-note", "care-session", NULL);
        status_row("agent.status", "care-session", "activity_code", "working");
    }
    if (scenario == 3 && posted == 1 && !queued && now - began_at >= 500000) {
        assert(s_stream->echo_row);
        queued = true; assert(gadget_events_post("Care Board", "call_request", NULL));
        assert(gadget_events_post("Care Board", "missed", NULL));
    }
}

static void scripted_worker_tests(void) {
    for (scenario = 1; scenario <= 3; scenario++) {
        reset(); posted = 0; echoed = queued = false; old_callback = NULL;
        began_at = now; post_hook = scripted_post; delay_hook = scripted_delay;
        assert(gadget_events_post("Care Board", "due", NULL));
        if (scenario == 2) {
            assert(gadget_events_post("Care Board", "ack", NULL));
            assert(gadget_events_post("Care Board", "missed", NULL));
        }
        run_worker = true;
        if (!setjmp(worker_end)) event_task(NULL);
        run_worker = false;
        assert(s_count == 0 && !strcmp(s_reply, "Confirmed."));
        if (scenario == 1) {
            assert(posted == 2 && posted_at[1] - began_at == SILENT_SETTLE_US);
            assert(!strcmp(posted_text[0], "[Care Board event] due"));
            assert(!strcmp(posted_text[1], "[Care Board event] ack"));
        } else {
            assert(posted == 3);
            assert(posted_at[1] - began_at == (scenario == 2 ? 200000 : 500000));
            assert(posted_at[2] == posted_at[1]);
            assert(!strcmp(posted_text[1], scenario == 2 ? "[Care Board event] ack" : "[Care Board event] call_request"));
            assert(!strcmp(posted_text[2], "[Care Board event] missed"));
        }
    }
    reset();
}

int main(void) {
    assert(gadget_events_init());
    silent_parser_tests();
    assert(gadget_events_console_command("event.debug=1"));
    assert(gadget_events_console_command("event=Silent confirmation"));
    event_t *event = dequeue(); post_hook = scripted_post;
    scenario = 1; posted = 0; int64_t before = now;
    assert(send_event(event) && now - before == SILENT_SETTLE_US && !s_reply[0]);
    release(event); reset();
    assert(gadget_events_console_command("event.debug=0"));
    scenario = posted = 0; post_hook = scripted_post;
    assert(gadget_events_post("Care Board", "working without a reply", NULL));
    event = dequeue(); before = now;
    assert(send_event(event) && now - before == REPLY_TIMEOUT_US);
    assert(s_stream->echo_row && s_stream->agent_busy && !s_stream->started && !s_reply[0]);
    release(event); reset();
    scripted_worker_tests();
    puts("ok");
    return 0;
}
