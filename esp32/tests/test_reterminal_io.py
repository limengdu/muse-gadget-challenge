# Copyright (c) 2026 limengdu
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from pathlib import Path
import tempfile
import unittest

try:
    from .card_test_support import ROOT, compile_host, run_host, write_rtos
except ImportError:
    from card_test_support import ROOT, compile_host, run_host, write_rtos


class ReterminalIO(unittest.TestCase):
    def test_keys_preserve_setup_and_hold_with_led_buzzer_and_shared_rtc(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            write_rtos(out)
            headers = {
                "esp_err.h": """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND -2
#define ESP_ERR_INVALID_STATE -3
static inline const char *esp_err_to_name(int e) {(void)e;return "fake";}
""",
                "sdkconfig.h": """#define CONFIG_HOMEHUB_RETERMINAL_IO 1
#define CONFIG_HOMEHUB_BUTTON_GPIO 3
#define CONFIG_HOMEHUB_VOICE 0
#define CONFIG_HOMEHUB_RETERMINAL_SHT4X_TEMP_OFFSET 0
""",
                "esp_attr.h": "#define IRAM_ATTR\n",
                "esp_log.h": """#include "sdkconfig.h"
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
""",
                "driver/gpio.h": """#pragma once
#include <stdint.h>
#include "esp_err.h"
#define GPIO_MODE_INPUT 0
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_ANYEDGE 3
typedef struct {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *);
int gpio_get_level(int);
esp_err_t gpio_set_level(int,int);
esp_err_t gpio_install_isr_service(int);
esp_err_t gpio_isr_handler_add(int,void (*)(void *),void *);
""",
                "driver/ledc.h": """#pragma once
#include "esp_err.h"
#define LEDC_TIMER_0 0
#define LEDC_CHANNEL_0 0
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_TIMER_10_BIT 10
#define LEDC_AUTO_CLK 0
typedef struct {int speed_mode,duty_resolution,timer_num,freq_hz,clk_cfg;} ledc_timer_config_t;
typedef struct {int gpio_num,speed_mode,channel,timer_sel,duty,hpoint;} ledc_channel_config_t;
esp_err_t ledc_timer_config(const ledc_timer_config_t *);
esp_err_t ledc_channel_config(const ledc_channel_config_t *);
esp_err_t ledc_set_duty(int,int,unsigned);
esp_err_t ledc_update_duty(int,int);
""",
                "driver/i2c_master.h": """#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#define I2C_NUM_0 0
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 7
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct {int i2c_port,sda_io_num,scl_io_num,clk_source,glitch_ignore_cnt;struct {bool enable_internal_pullup;} flags;} i2c_master_bus_config_t;
typedef struct {int dev_addr_length,device_address,scl_speed_hz;} i2c_device_config_t;
esp_err_t i2c_master_get_bus_handle(int,i2c_master_bus_handle_t *);
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *,i2c_master_bus_handle_t *);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t,const i2c_device_config_t *,i2c_master_dev_handle_t *);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t,const uint8_t *,size_t,uint8_t *,size_t,int);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t,const uint8_t *,size_t,int);
esp_err_t i2c_master_probe(i2c_master_bus_handle_t,unsigned,int);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t,uint8_t *,size_t,int);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t);
""",
            }
            for name, text in headers.items():
                p = out / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(text)
            p = out / "freertos/FreeRTOS.h"
            p.write_text('#include_next "freertos/FreeRTOS.h"\n#define portYIELD_FROM_ISR() ((void)0)\n'
                'typedef int portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED 0\n'
                '#define taskENTER_CRITICAL(lock) ((void)(lock))\n#define taskEXIT_CRITICAL(lock) ((void)(lock))\n')
            p = out / "freertos/task.h"
            p.write_text(p.read_text() + '\nvoid vTaskNotifyGiveFromISR(TaskHandle_t,BaseType_t *);\n')
            binary = compile_host(out, [ROOT / "tests/reterminal_io_harness.c", ROOT / "main/reterminal_io.c", ROOT / "main/button.c", ROOT / "main/reterminal_sht4x.c"], json=True, flags=["-include", str(out / "sdkconfig.h")])
            result = run_host(binary)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
