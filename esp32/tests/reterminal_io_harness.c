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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "reterminal_io.h"
#include "reterminal_sht4x.h"
#include "button.h"
#include "stack_monitor.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"

static TaskFunction_t io_task, button_task;
static int64_t now, until, press_at, release_at;
static bool allowed=true, white_mode;
static unsigned green, left, right, short_presses, doubles, holds, forever_waits, buzzer_on, buzzer_off, led_writes, buses;
static int led_level;
static jmp_buf done;
static uint8_t rtc_bytes[9];
int64_t esp_timer_get_time(void) {return now;}
BaseType_t xTaskCreate(TaskFunction_t fn,const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out) {
    (void)stack;(void)arg;(void)priority;
    if(!strcmp(name,"btn"))button_task=fn;else if(!strcmp(name,"reterminal_io"))io_task=fn;else if(strcmp(name,"sht4x"))assert(false);
    if(out)*out=(void *)1;return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) {(void)task;}
void vTaskNotifyGiveFromISR(TaskHandle_t task,BaseType_t *wake) {(void)task;*wake=pdFALSE;}
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t ticks) {
    (void)clear;if(ticks==portMAX_DELAY)forever_waits++;
    int64_t next=ticks==portMAX_DELAY?until:now+(int64_t)ticks*1000;
    if(white_mode && press_at>now && press_at<next)next=press_at;
    if(white_mode && release_at>now && release_at<next)next=release_at;
    if(next>=until)longjmp(done,1);
    assert(next>now);now=next;return 1;
}
void vTaskDelay(int ticks) {now+=(int64_t)ticks*1000;if(now>=until)longjmp(done,1);}
void stack_monitor_poll(stack_monitor_t *state) {(void)state;}
int gpio_get_level(int pin) {
    bool down=now>=press_at && now<release_at;
    if(pin==3)return !down;
    if(pin==4)return !(white_mode && down);
    if(pin==5)return 1;
    assert(false);return 1;
}
esp_err_t gpio_config(const gpio_config_t *cfg) {assert(cfg->pin_bit_mask);return ESP_OK;}
esp_err_t gpio_set_level(int pin,int level) {assert(pin==6);led_level=level;led_writes++;return ESP_OK;}
esp_err_t gpio_install_isr_service(int flags) {(void)flags;return ESP_OK;}
esp_err_t gpio_isr_handler_add(int pin,void (*handler)(void *),void *arg) {assert((pin==4||pin==5)&&handler);(void)arg;return ESP_OK;}
esp_err_t ledc_timer_config(const ledc_timer_config_t *cfg) {assert(cfg->freq_hz==2000 && cfg->duty_resolution==10);return ESP_OK;}
esp_err_t ledc_channel_config(const ledc_channel_config_t *cfg) {assert(cfg->gpio_num==45 && cfg->duty==0);return ESP_OK;}
esp_err_t ledc_set_duty(int mode,int channel,unsigned duty) {(void)mode;assert(channel==0);if(duty){assert(duty==512);buzzer_on++;}else buzzer_off++;return ESP_OK;}
esp_err_t ledc_update_duty(int mode,int channel) {(void)mode;(void)channel;return ESP_OK;}
esp_err_t i2c_master_get_bus_handle(int port,i2c_master_bus_handle_t *out) {(void)out;assert(port==0);return ESP_ERR_NOT_FOUND;}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *cfg,i2c_master_bus_handle_t *out) {
    assert(cfg->i2c_port==0 && cfg->sda_io_num==19 && cfg->scl_io_num==20);*out=(void *)16;buses++;return ESP_OK;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,const i2c_device_config_t *cfg,i2c_master_dev_handle_t *out) {
    assert(bus==(void *)16 && (cfg->device_address==0x51 || cfg->device_address==0x44) && cfg->scl_speed_hz==100000);*out=cfg->device_address==0x51?(void *)17:(void *)18;return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev,const uint8_t *write,size_t nw,uint8_t *read,size_t nr,int wait) {
    assert(dev==(void *)17 && nw==1 && write[0]==0 && nr==9 && wait==100);memcpy(read,rtc_bytes,9);return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev,const uint8_t *bytes,size_t n,int wait) {
    assert(dev==(void *)17 && wait==100 && bytes[0]+n-1<=9);
    memcpy(rtc_bytes+bytes[0],bytes+1,n-1);return ESP_OK;
}
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus,unsigned address,int wait) {assert(bus==(void *)16 && address==0x44 && wait==100);return ESP_OK;}
esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev,uint8_t *data,size_t n,int wait) {(void)dev;(void)data;(void)n;(void)wait;return ESP_FAIL;}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus) {(void)bus;assert(false);return ESP_OK;}
static bool ready(void) {return allowed;}
static void key(reterminal_key_t k) {if(k==RETERMINAL_GREEN)green++;else if(k==RETERMINAL_LEFT)left++;else right++;}
static void tap(void) {short_presses++;}
static void twice(void) {doubles++;}
static void hold(void) {holds++;}
static void run(TaskFunction_t fn,int64_t stop) {until=stop;if(!setjmp(done))fn(NULL);}
int main(void) {
    assert(button_init(tap,twice,hold));assert(reterminal_io_init(ready,key));
    assert(io_task&&button_task&&led_level==1);
    reterminal_sht4x_init();assert(buses==1);
    i2c_master_bus_handle_t a,b;assert(reterminal_io_i2c_bus(&a)&&reterminal_io_i2c_bus(&b)&&a==b&&buses==1);
    assert(reterminal_io_rtc_write(1730678400LL));int64_t epoch;assert(reterminal_io_rtc_read(&epoch)&&epoch==1730678400LL);
    now=0;press_at=0;release_at=150000;run(button_task,800000);assert(green==1&&!short_presses&&!holds);
    now=0;release_at=2100000;run(button_task,2800000);assert(green==2&&!short_presses&&!holds);
    allowed=false;now=0;release_at=150000;run(button_task,800000);assert(green==2&&short_presses==1);
    allowed=true;now=0;release_at=6000000;run(button_task,6500000);assert(holds==1&&green==2&&!doubles);
    white_mode=true;now=0;press_at=10000;release_at=100000;run(io_task,250000);assert(left==1&&!right&&forever_waits>0);
    now=0;press_at=10000;release_at=20000;run(io_task,150000);assert(left==1);
    white_mode=false;now=0;reterminal_io_chime(true);run(io_task,500000);assert(buzzer_on==2&&buzzer_off==2);
    now=0;reterminal_io_due(true);unsigned before=led_writes;run(io_task,1200000);assert(led_level==0&&led_writes>=before+3);
    now=0;reterminal_io_due(false);run(io_task,100000);assert(led_level==1);
    puts("ok");return 0;
}
