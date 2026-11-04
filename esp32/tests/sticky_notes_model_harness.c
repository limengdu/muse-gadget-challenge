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
#include "sticky_notes_model.h"

static sticky_notes_model_t model, before;
static cJSON *call(const char *command, const char *text, bool ok, bool expect_change) {
    cJSON *params = text ? cJSON_Parse(text) : NULL;
    bool changed = true;
    before = model;
    cJSON *result = sticky_notes_apply(&model, command, params, 1704067200LL, &changed);
    assert(result && cJSON_IsTrue(cJSON_GetObjectItem(result, "ok")) == ok);
    assert(changed == expect_change);
    if (!changed) assert(!memcmp(&model, &before, sizeof(model)));
    cJSON_Delete(params);
    return result;
}
static void run(const char *command, const char *text, bool ok, bool changed) {
    cJSON_Delete(call(command, text, ok, changed));
}
static void validation(void) {
    sticky_notes_model_init(&model);
    run("notes.get", NULL, true, false);
    const char *bad[] = {"null", "[]", "{}", "{\"list\":1,\"items\":[]}",
        "{\"list\":\"Shopping\",\"items\":[]}", "{\"list\":\"shopping\",\"items\":null}",
        "{\"list\":\"shopping\",\"items\":[null]}", "{\"list\":\"shopping\",\"items\":[\"\"]}",
        "{\"list\":\"shopping\",\"items\":[\"   \"]}", "{\"list\":\"shopping\",\"items\":[{\"text\":\"Eggs\",\"who\":1}]}",
        "{\"list\":\"shopping\",\"items\":[{\"text\":\"Eggs\",\"done\":\"yes\"}]}",
        "{\"list\":\"shopping\",\"items\":[{\"text\":\"Eggs\",\"id\":1.5}]}",
        "{\"list\":\"shopping\",\"items\":[{\"text\":\"Eggs\",\"added_at\":-1}]}",
        "{\"list\":\"shopping\",\"items\":[\"Eggs\"],\"extra\":true}",
        "{\"list\":\"shopping\",\"list\":\"chores\",\"items\":[]}",
        "{\"list\":\"shopping\",\"items\":[\"Line\\nBreak\"]}"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) run("notes.set", bad[i], false, false);
    run("notes.set", "{\"list\":\"shopping\",\"title\":\"Shopping\",\"items\":[\"Eggs\",{\"text\":\"Soy sauce\",\"who\":\"Leo\"}]}", true, true);
    assert(model.count == 1 && model.lists[0].count == 2 && model.lists[0].items[0].id == 1);
    assert(model.lists[0].items[1].added_at == 1704067200LL);
    const char *toggle_bad[] = {"{}", "{\"list\":\"shopping\",\"id\":0}",
        "{\"list\":\"shopping\",\"id\":1.1}", "{\"list\":\"shopping\",\"id\":999}",
        "{\"list\":\"shopping\",\"id\":1,\"done\":0}",
        "{\"list\":\"shopping\",\"id\":1,\"items\":[]}",
        "{\"list\":\"missing\",\"id\":1}"};
    for (unsigned i = 0; i < sizeof(toggle_bad) / sizeof(toggle_bad[0]); i++) run("notes.toggle", toggle_bad[i], false, false);
    run("notes.toggle", "{\"list\":\"shopping\",\"id\":1,\"done\":true}", true, true);
    run("notes.toggle", "{\"list\":\"shopping\",\"id\":1,\"done\":true}", true, false);
    run("notes.toggle", "{\"list\":\"shopping\",\"id\":1}", true, true);
    assert(!model.lists[0].items[0].done);
    run("notes.add", "{\"list\":\"shopping\",\"items\":[{\"text\":\"Milk\",\"done\":true}]}", false, false);
    run("notes.add", "{\"list\":\"shopping\",\"items\":[]}", false, false);
    run("notes.add", "{\"list\":\"chores\",\"items\":[{\"text\":\"Saturday cleanup\",\"who\":\"Leo\"}]}", true, true);
    run("notes.set", "{\"list\":\"todo\",\"items\":[{\"text\":\"Read\",\"id\":1}]}", false, false);
    run("notes.get", "{\"list\":\"chores\"}", true, false);
    run("notes.get", "{\"list\":\"missing\"}", false, false);
    run("notes.get", "{\"id\":1}", false, false);
    run("notes.remove", "{\"list\":\"shopping\",\"id\":2,\"done\":true}", false, false);
    run("notes.remove", "{\"list\":\"shopping\",\"id\":2}", true, true);
    run("notes.toggle", "{\"list\":\"shopping\",\"id\":1}", true, true);
    run("notes.clear_done", "{\"list\":\"shopping\"}", true, true);
    run("notes.clear_done", "{\"list\":\"shopping\"}", true, false);
    run("notes.set", "{\"list\":\"todo\",\"items\":[]}", true, true);
    run("notes.set", "{\"list\":\"notes\",\"items\":[]}", true, true);
    run("notes.set", "{\"list\":\"fifth\",\"items\":[]}", false, false);
    run("notes.unknown", "{}", false, false);
}
static void limits(void) {
    sticky_notes_model_init(&model);
    cJSON *params = cJSON_Parse("{\"list\":\"shopping\",\"items\":[]}");
    cJSON *items = cJSON_GetObjectItem(params, "items");
    char text[STICKY_NOTES_TEXT + 1]; memset(text, 'x', sizeof(text)); text[STICKY_NOTES_TEXT - 1] = 0;
    for (unsigned i = 0; i < STICKY_NOTES_ITEMS; i++) cJSON_AddItemToArray(items, cJSON_CreateString(text));
    bool changed;
    cJSON *result = sticky_notes_apply(&model, "notes.set", params, 0, &changed);
    assert(changed && model.lists[0].count == 20); cJSON_Delete(result);
    run("notes.add", "{\"list\":\"shopping\",\"items\":[\"Overflow\"]}", false, false);
    cJSON_AddItemToArray(items, cJSON_CreateString("Overflow"));
    result = sticky_notes_apply(&model, "notes.set", params, 0, &changed);
    assert(!changed); cJSON_Delete(result); cJSON_Delete(params);
    params = cJSON_Parse("{\"list\":\"shopping\",\"items\":[]}");
    text[STICKY_NOTES_TEXT - 1] = 'x'; text[STICKY_NOTES_TEXT] = 0;
    cJSON_AddItemToArray(cJSON_GetObjectItem(params, "items"), cJSON_CreateString(text));
    result = sticky_notes_apply(&model, "notes.set", params, 0, &changed);
    assert(!changed); cJSON_Delete(result); cJSON_Delete(params);
}
static void mirror(void) {
    sticky_notes_model_t restored;
    sticky_notes_model_init(&restored);
    cJSON *json = sticky_notes_json(&model, NULL);
    assert(sticky_notes_restore(&restored, json));
    assert(!memcmp(&model, &restored, sizeof(model)));
    char *text = cJSON_PrintUnformatted(json); cJSON_Delete(json);
    size_t offset = 0, length = strlen(text), written = 0, chunks = 0;
    char *combined = calloc(length + 1, 1);
    while (offset < length) {
        size_t previous = offset;
        char *body = sticky_notes_write_body(text, &offset);
        assert(body && strlen(body) < 2048);
        cJSON *request = cJSON_Parse(body); free(body);
        assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(request, "path")), STICKY_NOTES_PATH));
        assert(cJSON_IsTrue(cJSON_GetObjectItem(request, "overwrite")) == (chunks == 0));
        assert(cJSON_IsTrue(cJSON_GetObjectItem(request, "append")) == (chunks != 0));
        assert(cJSON_IsTrue(cJSON_GetObjectItem(request, "create_parent")));
        const char *part = cJSON_GetStringValue(cJSON_GetObjectItem(request, "text"));
        assert(strlen(part) == offset - previous && strlen(part) <= STICKY_NOTES_CHUNK);
        memcpy(combined + written, part, strlen(part)); written += strlen(part);
        cJSON_Delete(request); chunks++;
    }
    assert(chunks > 1 && written == length && !strcmp(text, combined));
    json = cJSON_Parse(combined); assert(sticky_notes_restore(&restored, json)); cJSON_Delete(json);
    free(text); free(combined);
    // A multi-byte scalar straddles the nominal chunk boundary.
    // 一个多字节字符跨越名义分块边界。
    char utf8[800]; memset(utf8, 'a', 767); memcpy(utf8 + 767, "\xe6\x96\xb0", 3); utf8[770] = 0;
    offset = 0; char *body = sticky_notes_write_body(utf8, &offset); assert(offset == 767); free(body);
    body = sticky_notes_write_body(utf8, &offset); assert(offset == 770); free(body);
    json = cJSON_Parse("{\"version\":1,\"revision\":1,\"next_id\":1,\"lists\":[{\"list\":\"x\",\"items\":[{\"id\":1,\"text\":\"Bad next ID\"}]}]}");
    assert(!sticky_notes_restore(&restored, json)); cJSON_Delete(json);
}
static int fail_after;
static void *failing_alloc(size_t n) {
    if (!fail_after) return NULL;
    if (fail_after > 0) fail_after--;
    return malloc(n);
}

// Injects allocation failures before committing any visible state.
// 在提交可见状态之前注入内存分配失败。
static void allocation(void) {
    cJSON *params = cJSON_Parse("{\"list\":\"shopping\",\"items\":[\"Eggs\"]}");
    cJSON_Hooks hooks = {failing_alloc, free};
    unsigned success = 0;
    for (int n = 0; n < 80; n++) {
        sticky_notes_model_init(&model); before = model;
        fail_after = n; cJSON_InitHooks(&hooks);
        bool changed;
        cJSON *result = sticky_notes_apply(&model, "notes.set", params, 0, &changed);
        cJSON_InitHooks(NULL);
        if (changed) { assert(result && cJSON_IsTrue(cJSON_GetObjectItem(result,"ok"))); success++; }
        else assert(!memcmp(&model, &before, sizeof(model)));
        cJSON_Delete(result);
        size_t offset = 0;
        fail_after = n; cJSON_InitHooks(&hooks);
        char *body = sticky_notes_write_body("{\"lists\":[]}", &offset);
        cJSON_InitHooks(NULL);
        if (!body) assert(offset == 0);
        else {
            cJSON *json = cJSON_Parse(body);
            assert(json && cJSON_IsTrue(cJSON_GetObjectItem(json,"overwrite")));
            assert(cJSON_IsFalse(cJSON_GetObjectItem(json,"append")));
            assert(cJSON_IsTrue(cJSON_GetObjectItem(json,"create_parent")));
            cJSON_Delete(json); free(body);
        }
    }
    assert(success); cJSON_Delete(params);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "validation")) validation();
    else if (!strcmp(argv[1], "limits")) limits();
    else if (!strcmp(argv[1], "allocation")) allocation();
    else { limits(); mirror(); }
    puts("ok");
}
