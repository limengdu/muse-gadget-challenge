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

#include "reterminal_io.h"

#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include "button.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// Sources: Seeed reTerminal E1001 V1.2 schematic, sheet GPIO/I2C/PWM, and
// Zephyr boards/seeed/reterminal_e1001/reterminal_e1001_procpu.dts (Apache-2.0).
// Reference behavior: codecraft-ai-hardware/esp32/boards/reterminal-e1001/bsp/
// include/bsp_pins.h, bsp_button.c, bsp_led.c, bsp_buzzer.c and bsp_rtc.c.
// 来源为 Seeed E1001 V1.2 原理图与 Zephyr 板级定义；上述参考驱动提供电平和时序。
#define KEY_LEFT_GPIO 4
#define KEY_RIGHT_GPIO 5
#define STATUS_GPIO 6
#define BUZZER_GPIO 45
#define I2C_SDA_GPIO 19
#define I2C_SCL_GPIO 20
#define RTC_ADDRESS 0x51
#define BUZZER_TIMER LEDC_TIMER_0
#define BUZZER_CHANNEL LEDC_CHANNEL_0

// ---- RTC protocol (host-tested) ---------------------------------------------
static bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
static int month_days(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1] + (month == 2 && leap(year));
}
static int bcd(uint8_t value) {
    return (value & 15) > 9 || (value >> 4) > 9 ? -1 : (value >> 4) * 10 + (value & 15);
}
static uint8_t to_bcd(unsigned value) { return (uint8_t)((value / 10) * 16 + value % 10); }

// PCF8563 registers 0x00..0x08 contain STOP, VL and the BCD UTC calendar.
// PCF8563 的 0x00 至 0x08 寄存器包含停钟标记、低电压标记和 BCD UTC 日历。
bool reterminal_io_rtc_decode(const uint8_t raw[9], int64_t *epoch) {
    if (!raw || !epoch || (raw[0] & 0x20) || (raw[2] & 0x80) || (raw[7] & 0x80)) return false;
    int second = bcd(raw[2] & 0x7F), minute = bcd(raw[3] & 0x7F), hour = bcd(raw[4] & 0x3F);
    int day = bcd(raw[5] & 0x3F), month = bcd(raw[7] & 0x1F), yy = bcd(raw[8]);
    if (second < 0 || second > 59 || minute < 0 || minute > 59 || hour < 0 || hour > 23
        || month < 1 || month > 12 || yy < 24 || yy > 99 || day < 1
        || day > month_days(2000 + yy, month) || (raw[6] & 7) > 6) return false;
    int64_t days = 0;
    for (int year = 1970; year < 2000 + yy; year++) days += 365 + leap(year);
    for (int m = 1; m < month; m++) days += month_days(2000 + yy, m);
    *epoch = ((days + day - 1) * 24 + hour) * 3600 + minute * 60 + second;
    return true;
}

bool reterminal_io_rtc_encode(int64_t epoch, uint8_t raw[8]) {
    if (!raw || epoch < 1704067200LL || epoch > 4102444799LL) return false;
    time_t at = (time_t)epoch; struct tm t;
    if (!gmtime_r(&at, &t)) return false;
    raw[0] = 0x02; raw[1] = to_bcd((unsigned)t.tm_sec); raw[2] = to_bcd((unsigned)t.tm_min);
    raw[3] = to_bcd((unsigned)t.tm_hour); raw[4] = to_bcd((unsigned)t.tm_mday);
    raw[5] = (uint8_t)t.tm_wday; raw[6] = to_bcd((unsigned)t.tm_mon + 1);
    raw[7] = to_bcd((unsigned)t.tm_year - 100);
    return true;
}
// ---- Hardware --------------------------------------------------------------
static const char *TAG = "link.reterminal_io";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_rtc;
static _Atomic(SemaphoreHandle_t) s_bus_mutex;
static TaskHandle_t s_task;
static reterminal_ready_cb s_ready;
static reterminal_key_cb s_key;
static atomic_bool s_due;
static atomic_int s_sound;
static bool s_buzzer_ready;

// Publishes one shared mutex for order-independent bus and RTC initialization.
// 发布共用互斥锁，使总线和 RTC 初始化不依赖调用先后顺序。
static SemaphoreHandle_t bus_mutex(void) {
    SemaphoreHandle_t mutex = atomic_load(&s_bus_mutex);
    if (!mutex) {
        SemaphoreHandle_t created = xSemaphoreCreateMutex();
        if (!created) return NULL;
        if (atomic_compare_exchange_strong(&s_bus_mutex, &mutex, created)) mutex = created;
        else vSemaphoreDelete(created);
    }
    return mutex;
}

bool reterminal_io_i2c_bus(i2c_master_bus_handle_t *out) {
    SemaphoreHandle_t mutex = bus_mutex();
    if (!out || !mutex) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!s_bus) {
        i2c_master_bus_config_t config = {
            .i2c_port = I2C_NUM_0, .sda_io_num = I2C_SDA_GPIO, .scl_io_num = I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        if (i2c_new_master_bus(&config, &s_bus) != ESP_OK) s_bus = NULL;
    }
    *out = s_bus;
    xSemaphoreGive(mutex);
    return *out != NULL;
}

static bool rtc_device(void) {
    i2c_master_bus_handle_t bus;
    if (!reterminal_io_i2c_bus(&bus)) return false;
    SemaphoreHandle_t mutex = bus_mutex();
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!s_rtc) {
        i2c_device_config_t config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = RTC_ADDRESS, .scl_speed_hz = 100000};
        if (i2c_master_bus_add_device(bus, &config, &s_rtc) != ESP_OK) s_rtc = NULL;
    }
    bool ready = s_rtc != NULL;
    xSemaphoreGive(mutex);
    return ready;
}

bool reterminal_io_rtc_read(int64_t *epoch) {
    if (!rtc_device()) return false;
    uint8_t reg = 0, raw[9];
    SemaphoreHandle_t mutex = bus_mutex();
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = i2c_master_transmit_receive(s_rtc, &reg, 1, raw, sizeof(raw), 100) == ESP_OK
        && reterminal_io_rtc_decode(raw, epoch);
    xSemaphoreGive(mutex);
    return ok;
}

bool reterminal_io_rtc_write(int64_t epoch) {
    uint8_t raw[8];
    if (!reterminal_io_rtc_encode(epoch, raw) || !rtc_device()) return false;
    const uint8_t stop[] = {0x00, 0x20}, start[] = {0x00, 0x00};
    SemaphoreHandle_t mutex = bus_mutex();
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = i2c_master_transmit(s_rtc, stop, sizeof(stop), 100) == ESP_OK
        && i2c_master_transmit(s_rtc, raw, sizeof(raw), 100) == ESP_OK;
    bool running = i2c_master_transmit(s_rtc, start, sizeof(start), 100) == ESP_OK;
    xSemaphoreGive(mutex);
    return ok && running;
}

static void tone(bool on) {
    if (!s_buzzer_ready) return;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, on ? 512 : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
}

void reterminal_io_due(bool due) {
    if (atomic_exchange(&s_due, due) != due && s_task) xTaskNotifyGive(s_task);
}
void reterminal_io_chime(bool gentle) {
    atomic_store(&s_sound, gentle ? 2 : 1);
    if (s_task) xTaskNotifyGive(s_task);
}
void reterminal_io_silence(void) {
    atomic_store(&s_sound, -1);
    if (s_task) xTaskNotifyGive(s_task);
}

static bool green_short(void) {
    if (!s_ready || !s_ready()) return false;
    s_key(RETERMINAL_GREEN); return true;
}

static void IRAM_ATTR key_interrupt(void *arg) {
    (void)arg;
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &wake);
    if (wake) portYIELD_FROM_ISR();
}

// Waits on key edges, debounce deadlines, LED pulses and buzzer segment deadlines.
// 等待按键边沿、消抖截止点、LED 闪烁和蜂鸣器分段截止点。
static void io_task(void *arg) {
    (void)arg;
    bool raw[2] = {false}, stable[2] = {false}, led = false;
    int64_t changed[2] = {0}, blink_at = 0, sound_at = 0;
    int sound = 0, segment = 0;
    const int pins[] = {KEY_LEFT_GPIO, KEY_RIGHT_GPIO};
    for (;;) {
        int64_t now = esp_timer_get_time(), deadline = INT64_MAX;
        for (int i = 0; i < 2; i++) {
            bool level = gpio_get_level(pins[i]) == 0;
            if (level != raw[i]) { raw[i] = level; changed[i] = now; }
            if (stable[i] != raw[i]) {
                if (now - changed[i] >= 30000) {
                    stable[i] = raw[i];
                    if (stable[i] && s_ready && s_ready()) s_key(i ? RETERMINAL_RIGHT : RETERMINAL_LEFT);
                } else if (deadline > changed[i] + 30000) deadline = changed[i] + 30000;
            }
        }
        bool due = atomic_load(&s_due);
        if (!due) { led = false; blink_at = 0; }
        else if (now >= blink_at) { led = !led; blink_at = now + 500000; }
        gpio_set_level(STATUS_GPIO, led ? 0 : 1);
        if (due && deadline > blink_at) deadline = blink_at;
        int requested = atomic_exchange(&s_sound, 0);
        if (requested) { sound = requested > 0 ? requested : 0; segment = 0; sound_at = now; if (!sound) tone(false); }
        if (sound && now >= sound_at) {
            if (segment == 0) { tone(true); segment = 1; sound_at = now + (sound == 2 ? 90000 : 60000); }
            else if (segment == 1 && sound == 2) { tone(false); segment = 2; sound_at = now + 100000; }
            else if (segment == 2) { tone(true); segment = 3; sound_at = now + 90000; }
            else { tone(false); sound = 0; }
        }
        if (sound && deadline > sound_at) deadline = sound_at;
        TickType_t wait = deadline == INT64_MAX ? portMAX_DELAY
            : pdMS_TO_TICKS((uint32_t)((deadline > now ? deadline - now : 0) / 1000 + 1));
        ulTaskNotifyTake(pdTRUE, wait ? wait : 1);
    }
}

bool reterminal_io_init(reterminal_ready_cb ready, reterminal_key_cb key) {
    if (!ready || !key || s_task) return false;
    s_ready = ready; s_key = key;
    gpio_config_t keys = {.pin_bit_mask = (1ULL << KEY_LEFT_GPIO) | (1ULL << KEY_RIGHT_GPIO),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_ANYEDGE};
    gpio_config_t led = {.pin_bit_mask = 1ULL << STATUS_GPIO, .mode = GPIO_MODE_OUTPUT};
    if (gpio_config(&keys) != ESP_OK || gpio_config(&led) != ESP_OK) return false;
    gpio_set_level(STATUS_GPIO, 1);
    ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BUZZER_TIMER, .freq_hz = 2000, .clk_cfg = LEDC_AUTO_CLK};
    ledc_channel_config_t channel = {.gpio_num = BUZZER_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BUZZER_CHANNEL, .timer_sel = BUZZER_TIMER, .duty = 0, .hpoint = 0};
    s_buzzer_ready = ledc_timer_config(&timer) == ESP_OK && ledc_channel_config(&channel) == ESP_OK;
    if (!s_buzzer_ready) ESP_LOGW(TAG, "buzzer initialization unavailable");
    if (!rtc_device()) ESP_LOGW(TAG, "RTC attach unavailable");
    if (xTaskCreate(io_task, "reterminal_io", 3072, NULL, 3, &s_task) != pdPASS) return false;
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    if (gpio_isr_handler_add(KEY_LEFT_GPIO, key_interrupt, NULL) != ESP_OK
        || gpio_isr_handler_add(KEY_RIGHT_GPIO, key_interrupt, NULL) != ESP_OK) return false;
    button_set_short_press_observer(green_short);
    return true;
}
