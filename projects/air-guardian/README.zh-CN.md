<!--
Copyright (c) 2026 limengdu

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# Air Guardian 空气守护者

[English](README.md) · [Muse 技能](SKILL.md) · [验证记录](VALIDATION.md)

在家一眼看懂空气状况，出门也能通过自己的 Muse 聊天了解房间。适合婴儿房、
宠物独自在家的客厅、长辈住处和家庭办公空间。你可以问：“宝宝房间现在怎么样？”
“卧室昨晚的空气好吗？”设备把读数变成简单提示，并在越过设定值和恢复时发消息。

## 硬件准备

使用 **SenseCAP Indicator D1S 或 D1Pro**、USB 电源和 Wi-Fi。RP2040 保留 Seeed
原厂固件，并连接随附的 **Grove AHT20**，才能取得温度和湿度。板载 SCD41 与
SGP40 提供 CO2 和 tVOC 指数，由 RP2040 转发。D1 和 D1L 没有内置空气传感器。
本项目复用现有串口读取、LCD 接线和顶部按键。

硬件依据见 [Seeed 硬件说明](https://wiki.seeedstudio.com/SenseCAP_Indicator_Dive_into_the_Hardware/)
和 [RP2040 源码](https://github.com/Seeed-Solution/SenseCAP_Indicator_RP2040)。
把设备放在要观察的房间，周围保持空气流通，与阳光直射、热源和人的呼气保持距离。

## 编译、安装与配对

在仓库的 `esp32/` 目录中，使用 ESP-IDF **v6.0.1**：

```sh
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf6.0_py3.14_env
. ~/esp/esp-idf/export.sh
tools/board.sh sensecap-indicator build
idf.py -B build-sensecap-indicator menuconfig
tools/board.sh sensecap-indicator build
```

在 menuconfig 的 **ESP32 Device SDK** 中填写自己的 SDK token。它可在 Muse 的
**Account > SDK tokens** 中创建，保存在已被 Git 忽略的构建目录 `sdkconfig` 内。
Indicator 配置启用 `HOMEHUB_AIR_GUARDIAN`、传感器、设备事件、本地时钟和共用字体图标；
其他设备的配置保持关闭。复用旧构建目录时，已有配置优先，可在 menuconfig 中启用本功能。

首次安装前，按[仓库备份说明](../../AGENTS.md#credentials-and-backups)备份设备，再运行：

```sh
tools/board.sh sensecap-indicator flash
```

在 Muse 中添加设备、填写 Wi-Fi，按顶部按键确认配对。[入门指南](../../README.zh-CN.md)
提供完整安装与配对步骤。连接成功后显示空气仪表盘；设置、配对、断线和错误期间显示原有
Muse 状态画面。

把 [SKILL.md](SKILL.md) 文件或原文交给 Muse，请它安装为 **Air Guardian** 技能。
随后让 Muse 用 `clock.set` 设置当前 UTC 时间和本地时差。联网校时负责 UTC，保存的时差
负责本地夜间时段；夏令时变化时更新时差。

## 家里的人怎么用

短按顶部按键依次切换 **氛围 → 数字 → 夜间 → 自动**，选择会保存。设置期间，短按仍用于
确认配对或打开设置窗口；长按五秒仍重置设置。重置会清除配对和 Wi-Fi，设备级设置保留。

- **氛围视图**：房间名、植物图标、绿黄橙红的空气圆环、状态词和 CO2。圆环按当前可用读数
  中最需要关注的一项着色，缺失的传感器会明确提示。
- **数字视图**：CO2、tVOC、温度、湿度四格大数字，下面是最近 24 小时、每小时平均 CO2
  的曲线。缺失数据处断开，纵向范围随已有数据调整。
- **夜间视图**：黑底、较暗的暖色 CO2 数字。自动模式在本地 22:00（含）至次日 07:00
  （不含）使用夜间视图，其他时间使用氛围视图。时钟尚未有效时显示氛围视图。
- **提醒状态**：边框每秒明暗切换，并显示建议，直到读数恢复。夜间提醒使用较暗的暖色边框。
  夜间通过像素颜色调暗文字，继续使用原有背光驱动。

Muse 仍可用 `display.draw_url` 或 `display.draw_file` 展示图片。连接正常时，
`display.show_animation` 会在五秒内返回空气仪表盘。显示图片期间继续采样和判断提醒。

## 可以对 Muse 说

- “把房间叫 Bedroom，CO2 超过 1000 ppm 时提醒我。”
- “狗在家时，客厅超过 28 度或者 CO2 超过 1500 就告诉我。”
- “显示四项数字。”“切到夜间。”“晚上自动变暗。”
- “昨晚卧室空气怎么样？用简单的话说说最差的时段。”
- “每天早上七点，告诉我卧室昨晚的空气情况。”

屏幕房间名支持 1～40 个可打印 ASCII 字符，例如 `Nursery`。一块设备对应一个房间。
阈值持续生效；“宠物在家时”的启用和恢复可由 Muse 按双方约定的时间安排执行。
宠物是否在家、晨报定时都由 Muse 的例行安排提供。

## 命令与数据

命令返回统一的 `ok` 和 `payload`，或 `error`。保存设置、整理历史等工作均在独立后台任务中执行。

| 命令 | 参数与结果 |
|---|---|
| `sensors.read` | 原有实时读数，包含单位和数据年龄；超时读数为空。 |
| `air.status` | 无参数；返回读数、等级、房间、设置视图、实际视图、时钟有效性、阈值及各项最后保存的提醒。 |
| `air.set_thresholds` | 下表阈值和 `room` 的任意非空子集；整体保存成功后生效。 |
| `air.set_view` | 必填 `view`：`ambient`、`numbers`、`night`、`auto`；保存选择。 |
| `sensors.history` | 可选整数 `hours` 为 1～168，默认 24；`step_min` 为 1～10080，默认 1；自动增大间隔，最多返回 24 点。 |
| `clock.set` | 整数 `utc_epoch`、`utc_offset_min`，时差范围 −840～+840 分钟。 |
| `events.poll` | 可选 `since`，补读共用事件模块保留的最近事件。 |

| 阈值 | 初始值 | 接受范围 | 恢复所需余量 |
|---|---:|---:|---:|
| `co2_ppm` | 1000 | 整数 400～10000 ppm | 比上限低 100 ppm |
| `tvoc` | 250 | 整数 1～500 指数 | 比上限低 25 |
| `temp_high_c` | 28 | −40～85 ℃ | 比上限低 1 ℃ |
| `temp_low_c` | 16 | −40～85 ℃ | 比下限高 1 ℃ |
| `humidity_high` | 65 | 0～100 % RH | 比上限低 5 个百分点 |
| `humidity_low` | 30 | 0～100 % RH | 比下限高 5 个百分点 |

温度上下限至少相差 2 ℃，湿度至少相差 10 个百分点。严格超过上限或低于下限触发提醒，
达到恢复余量的边界即可恢复。每项阈值发送一次越界消息，并为已成功加入队列的提醒发送一次
恢复消息。同一指标反复越界的提醒间隔至少 30 分钟，上下限共用冷却时间，持续超标时保持边框提示。冷却期内再次越界，
若一直超标，冷却结束后发送；若提前恢复，该次未播报的异常也不另发恢复消息。
读数缺失时保留已有提醒，并提示检查传感器连接。

房间名、阈值、视图和各项最后提醒保存到设备中，重启后恢复。启动时 UTC 无效，则从启动起
保守计算已恢复的提醒冷却期。历史占用约 315 KiB 外部内存，重启后从空记录开始。
每分钟保存一次各项最新有效读数，保留七天；这是分钟快照，不是这一分钟所有串口读数的平均值。

历史返回对齐的 `timestamps`、`co2`、`tvoc`、`temperature` 和 `humidity` 数组。
每项为 `[最小值,平均值,最大值,样本数]` 或 `null`，时间戳表示时间段起点。
跨时间段求平均时按样本数加权，`step_min` 是实际使用的间隔。UTC 有效时 `time_basis`
为 `epoch`，否则为 `uptime`；前者用当前 UTC 和样本实际年龄对应时间，因此校时后所有记录
统一调整。最后一个时间段可能较短。合并数据适合描述趋势和峰值，无法据此算出精确超标分钟数。

提醒以 `[Air Guardian event]` 开头，包含简短建议，以及 `kind`（`alert` 或 `recovery`）、
指标、阈值名、房间、读数和阈值。消息经[共用事件模块](../../esp32/docs/gadget-foundation.md)
进入设备所有者的 Muse 聊天。送达依赖电源、Wi-Fi 和 Muse 连接；共用模块待发送队列有容量上限，
且存放在内存中。

## 从什么阈值开始

[技能文件](SKILL.md#starting-settings)提供可直接使用的配置。结合房间用途和实际照护需求调整：
婴儿睡眠房间按 [Lullaby Trust](https://www.lullabytrust.org.uk/baby-safety/safer-sleep-information/room-temperature/)
资料从 16～20 ℃开始；宠物、长辈和办公场景的温度示例是可调整的日常提醒点。
CO2 用于了解通风；[HSE 指南](https://www.hse.gov.uk/ventilation/using-co2-monitors.htm)
建议在有人房间中持续超过 1500 ppm 时改善通风。tVOC 是相对指数，100 为典型值，
它的单位不是 ppm，也不能识别具体污染物。

把提示作为检查房间和人员状况的提醒。烟雾、一氧化碳、氧气、健康情况和宠物是否在家，需要
相应设备和照护检查。室外空气与天气适合时再开窗。本项目不自动控制窗户、加热器或风扇，
回复为文字，未加入语音或提示音。

历史平时存放在设备上；查询到的数据和提醒进入自己的 Muse 聊天。按家庭需要决定与谁分享。

## 验证

在 `esp32/` 目录运行：

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
CARD_PREVIEW_DIR=/tmp/air-preview python3 -m unittest tests.test_air_guardian
```

第二条命令生成 `air_ambient.ppm`、`air_numbers.ppm`、`air_night.ppm` 和 `air_alert.ppm`。
测试运行真实 C 代码，并检查内存访问和未定义行为，覆盖历史循环、分段合并、提醒变化、
参数校验、设置恢复、异步回复与命令注册。

在仓库根目录可重新生成字体图标：

```sh
CARD_ICON_FONT=$HOME/Desktop/muse-gadget-challenge/.cache/fonts/MaterialSymbolsRounded.ttf \
  $HOME/Desktop/muse-gadget-challenge/.venv/bin/python esp32/tools/gen_card_assets.py
```

当前实现的验证来自主机测试。后续在可正常构建的环境完成全新 Indicator 构建与固件大小检查，
再用一台 D1S/D1Pro 验证：配对、重连、顶部按键确认与重置、视图循环、拔掉传感器、各项越界与恢复、
重启保存、本地夜间切换、展示图片后返回。实机还需检查颜色、夜间亮度、边框闪动和七天内存使用。
记录代码版本、硬件版本、台数和结果。
