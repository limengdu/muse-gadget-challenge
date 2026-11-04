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

# Outdoor Air 户外空气顾问 — SenseCAP Indicator D1

[English](README.md) · [Muse 技能](SKILL.md) · [验证记录](VALIDATION.md)

放在家里、让家人抬眼就能看懂的户外空气看板。Muse 根据主人的位置和偏好，
用自己的工具查询户外空气，再把一份快照推送到 D1 的 3.95 英寸、480×480 LCD。
最后一次有效数据和所选视图会保存在设备中，重启后继续使用。

## 硬件与数据来源

本应用**只支持 SenseCAP Indicator D1**，使用 Link-only 的
`tools/board.sh sensecap-indicator` 配置。D1S 和 D1 Pro 不在支持范围内。
D1 没有板载 CO2/tVOC 传感器；本项目不使用室内传感器、Grove AHT20、
RP2040 读数或传感器 UART 数据。

共用板级配置中的 `CONFIG_HOMEHUB_SENSECAP_SENSORS` 保留原设置，供基础 SDK 使用。
本应用跳过 UART 读取，即使全部读数缺失或为 null，Outdoor Air 也独立工作。应用启用时只登记户外空气命令，
不登记基础 SDK 的室内传感器命令。本应用没有室内历史环、设备端天气查询、
自动刷新计划或类别通知。查询、定时和简短建议由 Muse 负责，设备安静展示结果。

## 构建

在 `esp32/` 中，使用 ESP-IDF **v6.0.1**：

```sh
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf6.0_py3.14_env
tools/board.sh sensecap-indicator build
```

板级配置启用可选的 `CONFIG_HOMEHUB_AIR_GUARDIAN` 应用、本地时钟和共用字体图标。
应用依赖 Indicator LCD、PSRAM 和设备请求功能。内部 `air_guardian_*` 文件名和
当前项目目录保持原路径。设备凭据通过
`idf.py -B build-sensecap-indicator menuconfig` 私下配置在 Git 忽略的构建目录中。
硬件部署遵循 SDK 的[设备指南](../../esp32/AGENTS.md)。

## 交给 Muse

把 [SKILL.md](SKILL.md) 交给 Muse，作为 **Outdoor Air** 技能。告诉 Muse 地点和
家庭偏好，请它查询当前户外空气并更新设备。晨间刷新等定时任务由 Muse 执行。

Muse 应先用实际当前 UTC 时间和主人的当前时区偏移调用
`clock.set {"utc_epoch":...,"utc_offset_min":...}`。观测时间来自数据源；
重复推送同一份报告时保留原观测时间。

## 命令

| 命令 | 作用 |
|---|---|
| `air.update` | 校验并保存一份完整的户外空气快照。 |
| `air.status {}` | 返回快照、数据年龄 `age_s`、是否需要刷新、时钟有效性和视图。 |
| `air.set_view {"view":"numbers"}` | 保存 `ambient`、`numbers` 或 `night` 视图。 |

`air.update` 示例；实际使用时替换为查询结果及真实观测时间：

```json
{
  "aqi": 42,
  "pm25": 7.1,
  "category": "good",
  "advice": "Enjoy your usual plans.",
  "place": "San Francisco",
  "updated_utc": 1793793600
}
```

- `aqi`、`pm25` 至少提供一项；省略或 null 表示缺失。AQI 是**美国 EPA 标准的
  0–500 整数**，PM2.5 为 **0–1000 ug/m3**。零是有效数值。
- 必填 `category`、`advice`、`updated_utc`。类别为 `good`、`moderate`、
  `unhealthy_sensitive`、`unhealthy`、`very_unhealthy`、`hazardous`，分别对应
  AQI 0–50、51–100、101–150、151–200、201–300、301–500，
  见 [AirNow 分类说明](https://www.airnow.gov/aqi/aqi-basics/)。有 AQI 时必须匹配；
  只有 PM2.5 时，由 Muse 按数据源的观测周期提供对应的美国 EPA 类别。
- `advice` 为 1–80 个可打印 ASCII 字符，可选地点 `place` 为 1–40 个。
  建议屏幕短句控制在约 25 个字符，较长文字以省略号收尾；城市名使用英文或转写。
- `updated_utc` 为观测时刻的 UTC 秒数，范围 1704067200–4102444799。
  比现有观测更旧、比有效设备时钟超前五分钟、重复字段和未知字段都会被拒绝。
- 每次更新完整替换快照，包含可选字段。快照和视图一起写入一条本地保存记录
  （NVS，键名 `outdoor_air`），成功后才对外生效。校验或保存失败时继续使用原结果。

首次更新前 `snapshot` 为 null。`age_s` 是距观测的秒数；时钟未知或早于观测时为 null。
年龄未知或超过三小时，`refresh_needed` 为 true。重启恢复后仍保留真实观测时间。

## 画面和顶部按钮

概览显示类别、醒目的 AQI（缺少 AQI 时显示 PM2.5）、短建议、地点、更新时间和类别色带。
数值视图同时显示两项指标，缺失的一项显示 `--`。夜间视图使用暗暖色文字和暗色带，
物理背光亮度沿用板级设置。

完成设置及配对处理后，短按顶部按钮依次切换“概览 → 数值 → 夜间 → 概览”。
所选视图重启后保留。时钟未同步时显示 UTC 观测时间和年龄未知标记；三小时后页头提示刷新。
设置、配对和错误时沿用 SDK 状态画面；重连期间继续显示保存的数据，等待 Muse 刷新。
Muse 图片临时覆盖看板，`display.show_animation {}` 会在五秒内返回看板。

## 本机验证

```sh
cd esp32
CARD_PREVIEW_DIR=/tmp/card-preview python3 -m unittest tests.test_air_guardian
```

四张原生渲染预览位于 `/tmp/card-preview/`：`outdoor_good.ppm`、
`outdoor_moderate.ppm`、`outdoor_unhealthy.ppm`、`outdoor_night.ppm`。
构建结果和待实机验证内容见 [VALIDATION.md](VALIDATION.md)。
