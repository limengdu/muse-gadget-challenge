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
#define time fake_time
#define settimeofday fake_settimeofday
#include "gadget_clock.c"

static time_t clock_value;
static char persisted[16];
static bool write_fail;
static int sntp_calls, command_results;

time_t fake_time(time_t *out) { if (out) *out = clock_value; return clock_value; }
int fake_settimeofday(const struct timeval *tv, const struct timezone *tz) {
    (void)tz; clock_value = tv->tv_sec; return 0;
}
bool config_get_str(const char *key, char *out, size_t size) {
    assert(!strcmp(key, "utc_offset_min"));
    if (!persisted[0]) return false;
    snprintf(out, size, "%s", persisted); return true;
}
bool config_set_str(const char *key, const char *value) {
    assert(!strcmp(key, "utc_offset_min"));
    if (write_fail) return false;
    snprintf(persisted, sizeof(persisted), "%s", value); return true;
}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config) {
    assert(!strcmp(config->servers[0], "pool.ntp.org") && config->start && !config->wait_for_sync);
    sntp_calls++; return ESP_OK;
}
BaseType_t xTaskCreate(TaskFunction_t task, const char *name, unsigned stack,
                       void *arg, unsigned priority, TaskHandle_t *handle) {
    (void)name; (void)stack; (void)priority; (void)handle; task(arg); return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { (void)task; }
void noise_ctrl_send_command_result(noise_ctrl_session_generation_t generation,
                                    const char *id, cJSON *result) {
    assert(generation == 7 && !strcmp(id, "clock-1"));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(result, "ok")));
    cJSON_Delete(result); command_results++;
}

int main(void) {
    gadget_clock_init();
    assert(!gadget_clock_valid() && gadget_clock_minutes_of_day() == -1);
    struct tm local;
    assert(!gadget_clock_local(&local) && !gadget_clock_local(NULL));
    assert(gadget_clock_set(1704067200LL, 480));
    assert(gadget_clock_valid() && gadget_clock_epoch() == 1704067200LL);
    assert(gadget_clock_local(&local) && local.tm_hour == 8 && local.tm_min == 0);
    assert(gadget_clock_minutes_of_day() == 480 && !strcmp(persisted, "480"));
    atomic_store(&s_offset, 0); gadget_clock_init();
    assert(gadget_clock_minutes_of_day() == 480);
    assert(gadget_clock_set(1704067200LL, -60));
    assert(gadget_clock_local(&local) && local.tm_hour == 23 && local.tm_mday == 31 && local.tm_year == 123);
    assert(!gadget_clock_set(1, 0) && !gadget_clock_set(1704067200LL, 841));
    write_fail = true;
    assert(!gadget_clock_set(1704067200LL, 100));
    assert(gadget_clock_minutes_of_day() == 1380);
    write_fail = false;
    gadget_clock_wifi_up(); gadget_clock_wifi_up(); assert(sntp_calls == 1);
    const char *invalid[] = {"{}", "{\"utc_epoch\":1,\"utc_offset_min\":0}",
        "{\"utc_epoch\":1704067200,\"utc_offset_min\":0.5}",
        "{\"utc_epoch\":1704067200,\"utc_offset_min\":-841}",
        "{\"utc_epoch\":\"1704067200\",\"utc_offset_min\":0}"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        cJSON *params = cJSON_Parse(invalid[i]);
        cJSON *result = gadget_clock_command(params, "clock-1", 7);
        assert(cJSON_IsFalse(cJSON_GetObjectItem(result, "ok")));
        cJSON_Delete(result); cJSON_Delete(params);
    }
    cJSON *params = cJSON_Parse("{\"utc_epoch\":1704067200,\"utc_offset_min\":480}");
    cJSON *result = gadget_clock_command(params, "clock-1", 7);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(result, "_async")) && command_results == 1);
    cJSON_Delete(result); cJSON_Delete(params);
    puts("ok");
    return 0;
}
