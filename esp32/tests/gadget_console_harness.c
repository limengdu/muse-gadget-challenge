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
#include "sdk_token_console.c"

static unsigned handled, stored, restarted, delayed;
static char last[600];
static bool hook(const char *line) {
    handled++;snprintf(last,sizeof(last),"%s",line);return !strncmp(line,"event=",6);
}
bool identity_sdk_token_valid(const char *token) {return !strcmp(token,"fixture");}
bool identity_set_sdk_token(const char *token) {assert(!strcmp(token,"fixture"));stored++;return true;}
void esp_restart(void) {restarted++;}
void vTaskDelay(int t) {delayed+=(unsigned)t;}
BaseType_t xTaskCreate(TaskFunction_t task,const char *name,unsigned stack,
                       void *arg,unsigned priority,TaskHandle_t *out) {
    (void)task;(void)name;(void)stack;(void)arg;(void)priority;(void)out;return pdPASS;
}
int uart_driver_install(int port,int rx,int tx,int queue,void *event,int flags) {
    (void)port;(void)rx;(void)tx;(void)queue;(void)event;(void)flags;return ESP_OK;
}
void uart_vfs_dev_use_driver(int p) {(void)p;}
int uart_read_bytes(int port,void *data,unsigned bytes,int ticks) {
    (void)port;(void)data;(void)bytes;(void)ticks;return 0;
}
static void line(line_reader_t *reader,const char *text) {for(;*text;text++)feed(reader,*text);}
int main(void) {
    line_reader_t reader={0};
    assert(sdk_token_console_register_handler(hook));
    assert(sdk_token_console_register_handler(hook));
    assert(!sdk_token_console_register_handler(NULL));
    line(&reader,">event=Button ready\r\n");assert(handled==1&&!strcmp(last,"event=Button ready"));
    line(&reader,"ignored\n");assert(handled==1);
    line(&reader,">sdk_token=invalid\n");assert(!stored&&!restarted&&handled==1);
    line(&reader,">sdk_token=fixture\n");assert(stored==1&&restarted==1&&delayed==500&&handled==1);
    feed(&reader,'>');for(int i=0;i<COMMAND_LINE_MAX+10;i++)feed(&reader,'x');feed(&reader,'\n');
    assert(handled==1);
    line(&reader,">event=Recovered\n");assert(handled==2&&!strcmp(last,"event=Recovered"));
    assert(!sdk_token_console_dispatch(NULL));
    puts("ok");return 0;
}
