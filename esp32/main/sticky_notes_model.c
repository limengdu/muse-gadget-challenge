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

#include "sticky_notes_model.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

static const char *const commands[] = {
    "notes.set", "notes.add", "notes.get", "notes.toggle", "notes.remove", "notes.clear_done"
};

static cJSON *error(const char *message) {
    cJSON *result = cJSON_CreateObject(), *detail = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", false);
    cJSON_AddStringToObject(detail, "code", "invalid_params");
    cJSON_AddStringToObject(detail, "message", message);
    cJSON_AddItemToObject(result, "error", detail);
    return result;
}

static const cJSON *field(const cJSON *object, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

// Accepts complete UTF-8 scalars and visible, single-line text within byte limits.
// 接受完整 UTF-8 字符及字节上限内的可见单行文本。
static bool text_valid(const cJSON *value, size_t cap) {
    if (!cJSON_IsString(value) || !value->valuestring) return false;
    const unsigned char *s = (const unsigned char *)value->valuestring;
    size_t len = strlen((const char *)s);
    if (!len || len >= cap) return false;
    bool visible = false;
    for (size_t i = 0; i < len;) {
        unsigned c = s[i++], cp = c, n = 0, min = 0;
        if (c < 0x20 || c == 0x7f) return false;
        if (c >= 0x80) {
            if (c >= 0xc2 && c <= 0xdf) { n = 1; cp &= 0x1f; min = 0x80; }
            else if (c >= 0xe0 && c <= 0xef) { n = 2; cp &= 0x0f; min = 0x800; }
            else if (c >= 0xf0 && c <= 0xf4) { n = 3; cp &= 7; min = 0x10000; }
            else return false;
            if (i + n > len) return false;
            while (n--) {
                if ((s[i] & 0xc0) != 0x80) return false;
                cp = (cp << 6) | (s[i++] & 0x3f);
            }
            if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        }
        if (cp != ' ') visible = true;
    }
    return visible;
}

static bool integer(const cJSON *v, double min, double max) {
    return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble >= min
        && v->valuedouble <= max && floor(v->valuedouble) == v->valuedouble;
}

static bool fields(const cJSON *object, const char *const *allowed, unsigned count) {
    if (!cJSON_IsObject(object)) return false;
    const cJSON *v;
    cJSON_ArrayForEach(v, object) {
        unsigned i = 0;
        for (; i < count && strcmp(v->string, allowed[i]); i++) {}
        if (i == count) return false;
        for (const cJSON *prior = object->child; prior != v; prior = prior->next)
            if (!strcmp(prior->string, v->string)) return false;
    }
    return true;
}

// Allocates the bounded list snapshot in board PSRAM or host heap memory.
// 在板载外部内存或主机堆内存中分配有界清单快照。
sticky_notes_model_t *sticky_notes_model_alloc(void) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, sizeof(sticky_notes_model_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, sizeof(sticky_notes_model_t));
#endif
}

void sticky_notes_model_init(sticky_notes_model_t *model) {
    memset(model, 0, sizeof(*model));
    model->version = 1;
    model->next_id = 1;
}

bool sticky_notes_handles(const char *command) {
    if (!command) return false;
    for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
        if (!strcmp(command, commands[i])) return true;
    return false;
}

static int find_list(const sticky_notes_model_t *model, const char *name) {
    for (unsigned i = 0; i < model->count; i++)
        if (!strcmp(model->lists[i].name, name)) return (int)i;
    return -1;
}

cJSON *sticky_notes_json(const sticky_notes_model_t *model, const char *name) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    if (!cJSON_AddNumberToObject(root, "version", model->version)
        || !cJSON_AddNumberToObject(root, "revision", model->revision)
        || !cJSON_AddNumberToObject(root, "next_id", model->next_id)) goto fail;
    cJSON *lists = cJSON_AddArrayToObject(root, "lists");
    if (!lists) goto fail;
    for (unsigned l = 0; l < model->count; l++) {
        const sticky_notes_list_t *list = &model->lists[l];
        if (name && strcmp(name, list->name)) continue;
        cJSON *row = cJSON_CreateObject();
        if (!row) goto fail;
        if (!cJSON_AddItemToArray(lists, row)) { cJSON_Delete(row); goto fail; }
        if (!cJSON_AddStringToObject(row, "list", list->name)
            || !cJSON_AddStringToObject(row, "title", list->title)) goto fail;
        cJSON *items = cJSON_AddArrayToObject(row, "items");
        if (!items) goto fail;
        for (unsigned i = 0; i < list->count; i++) {
            const sticky_notes_item_t *item = &list->items[i];
            cJSON *v = cJSON_CreateObject();
            if (!v) goto fail;
            if (!cJSON_AddItemToArray(items, v)) { cJSON_Delete(v); goto fail; }
            if (!cJSON_AddNumberToObject(v, "id", item->id)
                || !cJSON_AddStringToObject(v, "text", item->text)
                || !cJSON_AddBoolToObject(v, "done", item->done)
                || (item->who[0] && !cJSON_AddStringToObject(v, "who", item->who))
                || !cJSON_AddNumberToObject(v, "added_at", (double)item->added_at)) goto fail;
        }
    }
    return root;
fail:
    cJSON_Delete(root);
    return NULL;
}

static bool ids_valid(const sticky_notes_model_t *model) {
    uint32_t ids[STICKY_NOTES_LISTS * STICKY_NOTES_ITEMS];
    unsigned count = 0;
    for (unsigned l = 0; l < model->count; l++) {
        for (unsigned i = 0; i < model->lists[l].count; i++) {
            uint32_t id = model->lists[l].items[i].id;
            if (!id || id >= model->next_id) return false;
            for (unsigned n = 0; n < count; n++) if (ids[n] == id) return false;
            ids[count++] = id;
        }
    }
    return true;
}

// Validates command fields and edits a private model snapshot.
// 校验命令字段并修改独立的清单快照。
static const char *mutate(sticky_notes_model_t *model, const char *command,
                          const cJSON *params, int64_t now) {
    const char *const allowed[] = {"list", "title", "items", "id", "done"};
    if (!fields(params, allowed, 5)) return "Expected a parameters object with known fields";
    const cJSON *name = field(params, "list"), *title = field(params, "title");
    const cJSON *items = field(params, "items"), *id = field(params, "id"), *done = field(params, "done");
    if (!text_valid(name, STICKY_NOTES_NAME)) return "list must be a short lowercase slug";
    for (const char *s = name->valuestring; *s; s++)
        if (!(*s >= 'a' && *s <= 'z') && !(*s >= '0' && *s <= '9') && *s != '-')
            return "list must contain lowercase letters, digits or hyphens";
    bool set = !strcmp(command, "notes.set"), add = !strcmp(command, "notes.add");
    bool toggle = !strcmp(command, "notes.toggle"), remove = !strcmp(command, "notes.remove");
    if (title && (!set || !text_valid(title, STICKY_NOTES_TITLE))) return "title is valid only for notes.set";
    if ((set || add) != (items != NULL)) return "items is required only for notes.set and notes.add";
    if ((toggle || remove) != (id != NULL) || (id && !integer(id, 1, INT32_MAX - 1)))
        return "id must be a positive item integer for toggle or remove";
    if (done && (!toggle || !cJSON_IsBool(done))) return "done must be boolean for notes.toggle";
    int index = find_list(model, name->valuestring);
    if (index < 0) {
        if (!set && !add) return "List not found";
        if (model->count == STICKY_NOTES_LISTS) return "At most 4 lists are supported";
        index = (int)model->count++;
        snprintf(model->lists[index].name, STICKY_NOTES_NAME, "%s", name->valuestring);
        snprintf(model->lists[index].title, STICKY_NOTES_TITLE, "%s", title ? title->valuestring : name->valuestring);
        model->lists[index].title[0] = (char)toupper((unsigned char)model->lists[index].title[0]);
    }
    sticky_notes_list_t *list = &model->lists[index];
    if (title) snprintf(list->title, sizeof(list->title), "%s", title->valuestring);
    if (set || add) {
        if (!cJSON_IsArray(items) || cJSON_GetArraySize(items) > STICKY_NOTES_ITEMS
            || (add && !cJSON_GetArraySize(items))) return "items must be an array of up to 20 items";
        if (set) { memset(list->items, 0, sizeof(list->items)); list->count = 0; }
        if (list->count + cJSON_GetArraySize(items) > STICKY_NOTES_ITEMS) return "List is full (20 items)";
        const cJSON *v;
        cJSON_ArrayForEach(v, items) {
            const cJSON *text = cJSON_IsString(v) ? v : field(v, "text");
            const cJSON *who = field(v, "who"), *item_id = field(v, "id");
            const cJSON *item_done = field(v, "done"), *at = field(v, "added_at");
            const char *const item_fields[] = {"text", "who", "id", "done", "added_at"};
            if ((!cJSON_IsString(v) && !fields(v, item_fields, set ? 5 : 2))
                || !text_valid(text, STICKY_NOTES_TEXT) || (who && !text_valid(who, STICKY_NOTES_WHO))
                || (item_id && !integer(item_id, 1, INT32_MAX - 1))
                || (item_done && !cJSON_IsBool(item_done)) || (at && !integer(at, 0, 4102444799LL)))
                return "Invalid item: check text, who, id, done and added_at limits";
            sticky_notes_item_t *item = &list->items[list->count++];
            memset(item, 0, sizeof(*item));
            if (model->next_id >= INT32_MAX) return "Item IDs exhausted";
            item->id = item_id ? (uint32_t)item_id->valuedouble : model->next_id++;
            if (item->id >= model->next_id) model->next_id = item->id + 1;
            item->done = cJSON_IsTrue(item_done);
            item->added_at = at ? (int64_t)at->valuedouble : now;
            snprintf(item->text, sizeof(item->text), "%s", text->valuestring);
            if (who) snprintf(item->who, sizeof(item->who), "%s", who->valuestring);
        }
        if (!ids_valid(model)) return "Item IDs must be unique across lists";
    } else if (toggle || remove) {
        unsigned i = 0;
        for (; i < list->count && list->items[i].id != (uint32_t)id->valuedouble; i++) {}
        if (i == list->count) return "Item not found";
        if (toggle) list->items[i].done = done ? cJSON_IsTrue(done) : !list->items[i].done;
        else {
            memmove(&list->items[i], &list->items[i + 1], (list->count - i - 1) * sizeof(list->items[0]));
            memset(&list->items[--list->count], 0, sizeof(list->items[0]));
        }
    } else {
        unsigned n = 0;
        for (unsigned i = 0; i < list->count; i++) if (!list->items[i].done) list->items[n++] = list->items[i];
        memset(&list->items[n], 0, (STICKY_NOTES_ITEMS - n) * sizeof(list->items[0]));
        list->count = n;
    }
    return NULL;
}

cJSON *sticky_notes_apply(sticky_notes_model_t *model, const char *command,
                          const cJSON *params, int64_t now, bool *changed) {
    *changed = false;
    if (!sticky_notes_handles(command)) return error("Unknown notes command");
    const char *name = NULL;
    sticky_notes_model_t *next = NULL;
    if (!strcmp(command, "notes.get")) {
        const char *const allowed[] = {"list"};
        if (params && !fields(params, allowed, 1)) return error("Expected optional list only");
        const cJSON *v = field(params, "list");
        if (v && !text_valid(v, STICKY_NOTES_NAME)) return error("Invalid list");
        name = v ? v->valuestring : NULL;
        if (name && find_list(model, name) < 0) return error("List not found");
    } else {
        next = sticky_notes_model_alloc();
        if (!next) return error("Out of memory");
        memcpy(next, model, sizeof(*next));
        const char *message = mutate(next, command, params, now);
        if (message) { free(next); return error(message); }
        *changed = memcmp(next, model, sizeof(*next)) != 0;
        if (*changed) {
            if (next->revision == UINT32_MAX) { free(next); *changed = false; return error("Revision exhausted"); }
            next->revision++;
        }
        name = field(params, "list")->valuestring;
    }
    cJSON *result = cJSON_CreateObject();
    cJSON *payload = sticky_notes_json(next ? next : model, name);
    if (!result || !payload || !cJSON_AddBoolToObject(result, "ok", true)
        || !cJSON_AddItemToObject(result, "payload", payload)) {
        cJSON_Delete(result); cJSON_Delete(payload); free(next);
        *changed = false;
        return error("Out of memory");
    }
    if (*changed) memcpy(model, next, sizeof(*model));
    free(next);
    return result;
}

// Reconstructs persisted lists through the same item validation as notes.set.
// 使用与 notes.set 相同的条目校验重建持久化清单。
bool sticky_notes_restore(sticky_notes_model_t *model, const cJSON *json) {
    if (!cJSON_IsObject(json) || !integer(field(json, "version"), 1, 1)
        || !integer(field(json, "revision"), 0, UINT32_MAX)
        || !integer(field(json, "next_id"), 1, INT32_MAX)) return false;
    const cJSON *lists = field(json, "lists");
    if (!cJSON_IsArray(lists) || cJSON_GetArraySize(lists) > STICKY_NOTES_LISTS) return false;
    sticky_notes_model_t *next = sticky_notes_model_alloc();
    if (!next) return false;
    sticky_notes_model_init(next);
    const cJSON *list;
    bool ok = true;
    cJSON_ArrayForEach(list, lists) {
        const cJSON *name = field(list, "list");
        if (!text_valid(name, STICKY_NOTES_NAME) || find_list(next, name->valuestring) >= 0
            || mutate(next, "notes.set", list, 0)) { ok = false; break; }
    }
    next->revision = (uint32_t)field(json, "revision")->valuedouble;
    next->next_id = (uint32_t)field(json, "next_id")->valuedouble;
    ok = ok && ids_valid(next);
    if (ok) memcpy(model, next, sizeof(*model));
    free(next);
    return ok;
}

char *sticky_notes_write_body(const char *json, size_t *offset) {
    size_t length = strlen(json);
    if (*offset >= length) return NULL;
    size_t n = length - *offset;
    if (n > STICKY_NOTES_CHUNK) n = STICKY_NOTES_CHUNK;
    if (*offset + n < length) while (n && ((unsigned char)json[*offset + n] & 0xc0) == 0x80) n--;
    if (!n) return NULL;
    char *part = malloc(n + 1);
    if (!part) return NULL;
    memcpy(part, json + *offset, n); part[n] = 0;
    cJSON *body = cJSON_CreateObject();
    bool complete = body && cJSON_AddStringToObject(body, "path", STICKY_NOTES_PATH)
        && cJSON_AddStringToObject(body, "text", part)
        && cJSON_AddBoolToObject(body, "overwrite", *offset == 0)
        && cJSON_AddBoolToObject(body, "append", *offset != 0)
        && cJSON_AddBoolToObject(body, "create_parent", true);
    free(part);
    if (!complete) { cJSON_Delete(body); return NULL; }
    char *result = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (result) *offset += n;
    return result;
}

static void param(cJSON *object, const char *key, const char *type, const char *description) {
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", type);
    cJSON_AddStringToObject(p, "description", description);
    cJSON_AddItemToObject(object, key, p);
}

void sticky_notes_register(cJSON *commands_object) {
    const char *descriptions[] = {
        "Replace one fridge list; optional title. Returns the list.",
        "Append fridge items; creates list. Returns the list.",
        "Read fridge lists, including done items. Optional list slug.",
        "Set done explicitly, or toggle one fridge item. Returns the list.",
        "Remove one fridge item by ID. Returns the list.",
        "Remove completed items from one fridge list. Returns the list."
    };
    for (unsigned i = 0; i < 6; i++) {
        cJSON *command = cJSON_CreateObject(), *required = cJSON_CreateObject(), *optional = cJSON_CreateObject();
        cJSON_AddStringToObject(command, "description", descriptions[i]);
        cJSON_AddItemToObject(command, "required", required);
        cJSON_AddItemToObject(command, "optional", optional);
        param(i == 2 ? optional : required, "list", "string", "Lowercase slug, 1-23 bytes; up to 4 lists.");
        if (i < 2) param(required, "items", "array", i == 0
            ? "0-20 strings or {text,who?,id?,done?,added_at?}; text 1-127, who 1-31 UTF-8 bytes."
            : "1-20 strings or {text,who?}; text 1-127, who 1-31 UTF-8 bytes. 20 per list.");
        if (i == 0) param(optional, "title", "string", "Display title, 1-31 UTF-8 bytes.");
        if (i == 3 || i == 4) param(required, "id", "integer", "Item ID from notes.get, 1-2147483646.");
        if (i == 3) param(optional, "done", "boolean", "Target checkbox state.");
        cJSON_AddItemToObject(commands_object, commands[i], command);
    }
}
