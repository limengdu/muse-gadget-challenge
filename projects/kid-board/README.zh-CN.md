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

# Kid Board：孩子的一天

用于 **reTerminal E1002** 的彩色时间线和星星奖励板，屏幕为 800×480 六色电子纸。
家长在外通过 Muse 更新安排、送上鼓励；孩子完成一步后按绿色键，Muse 把确认消息告诉主人。

[English](README.md) · [Muse 使用技能](SKILL.md) · [验证记录](VALIDATION.md)

## 构建、安装与配对

1. 按[构建指南](../../README.zh-CN.md)安装并启用 **ESP-IDF v6.0.1**。
2. 在 `esp32/` 执行 `tools/board.sh reterminal-e1002 build`。
3. 执行 `idf.py -B build-reterminal-e1002 menuconfig`，在 ESP32 Device SDK 的
   SDK token 项填写账号凭据，再构建一次。凭据保存在已忽略的
   `build-reterminal-e1002/sdkconfig`，由使用者在本机配置。
4. 按[设备指南](../../esp32/AGENTS.md#flash)备份原有固件，再执行
   `tools/board.sh reterminal-e1002 flash` 安装。
5. 在 Muse 应用添加 `MuseGadget-Disp-…`，填写 Wi-Fi，用绿色键确认配对。
   把 [SKILL.md](SKILL.md) 全文发给主人的 Muse，并指明使用这台 E1002。
6. 告诉 Muse 孩子的名字、所在地时区、允许送星星的人、每日上限和奖励约定。
   Muse 通过 `clock.set` 设置世界标准时间和本地时差。

E1002 配置启用 `HOMEHUB_KID_BOARD` 和体积优化，复用时钟、RTC、按键、蜂鸣器、
卡片绘图和 Muse 事件服务。E1001 保留 Care Board。固件继续使用固定的双 2 MB
应用分区及 CodeCraft Hub 烧录布局。

## 可以这样对 Muse 说

- “给 Mia 设置儿童板。我和奶奶每天最多送三颗星，集齐五颗去公园野餐。”
- “今天游泳取消，改成下午四点钢琴课，爷爷接她。”
- “Mia 写完作业了，给她一颗星。”
- “明早七点起床，刷牙、穿衣、早餐，七点四十出门。每做完一项按一下。”
- “今天 Mia 确认了哪些事？她有几颗星了？”

Muse 先读取 `kid.state`，保留已有条目的编号，再用 `kid.set_schedule` 更新完整安排。
送星星时先核对主人的规则，读取现有总数，最后用 `kid.set_stars` 写入新总数。
技能文档包含晚间安排明天和每周回顾的方法。

## 按键与页面

| 操作 | 设置完成后的效果 |
|---|---|
| 短按绿色键 | 确认最早已到时间的未完成步骤；都未到时，确认今天的下一步 |
| 配对时按绿色键 | 确认配对；设置完成且配对确认结束后启用儿童板操作 |
| 长按绿色键五秒 | 使用 SDK 原有流程重置设备设置 |
| 白色键 | 保持时间线页面 |
| 蜂鸣器 | 成功确认后响一声短提示音 |

确认后显示大绿勾，并通过 `gadget_events` 发出 `kid.done {id, at}`。
Muse 收到的通知文字包含孩子名字和步骤内容，再转告主人。事件未能入队时，
步骤仍可再次按键确认。成功按键间隔至少两秒。绿色键只改变步骤完成情况，星星由家长授予。

页首显示孩子名字、当天星期日期、本地时间和计划日期。最多十张卡片分两排；
当前条目用黄色底和更大图标突出，已完成条目显示绿色勾。底部展示最多二十颗星、
总数与目标、奖励和家长的一行留言。达到或超过目标时显示“Reward earned!”。
较长文字会缩小或以省略号收尾。

六色整屏刷新约需三十秒。指令保存成功后状态立即更新，连续改动合并，实际绘图开始
间隔至少六十秒；空闲窗口后的首次更新可立即绘图。页面开启时，时钟每分钟也通过
同一窗口申请更新。内容相同的命令保持原画面和存储记录。刷新期间，画面和时间可能稍有延迟。

## 命令与限制

| 命令 | 参数 |
|---|---|
| `kid.set_schedule` | 必填 `items`（0～10 项），可选 `child`、`date` |
| `kid.set_stars` | 必填当前总数 `count`、目标 `goal`；可选 `reward_label`、`reward_icon`、`note` |
| `kid.mark` | 已有条目的 `id` 和布尔值 `done`，返回更新后的状态 |
| `kid.state` | `{}`；返回安排、`done`/`done_at`、当前条目或空值、星星奖励、时钟有效性及存储/RTC 状态 |

每项包含 `id`（1～31 字节英文字母、数字、下划线或连字符）、`time`（`HH:MM`）、
`label`（1～47 字节）、`icon`（1～31 字节字母数字、下划线或连字符），
可选 `who`（0～31 字节）。编号唯一，按时间排序；同一时刻保持输入顺序。短词显示最清楚。

`child` 为 1～31 字节。`date` 为 2024～2099 年真实本地日期 `YYYY-MM-DD`；
省略时取今天，需要有效时钟。同一计划日期里，相同编号保留完成情况，包括修改时间或文字；
换一个计划日期则清空完成标记。明天的计划可提前显示，到日期后才接受绿色键确认。
本地跨日时，早于今天的计划顺延到今天，并重置步骤；Muse 更新前继续沿用最后一份安排。
晚上设置明天计划会立即替换当前显示。同一天新增的一次任务使用新编号。

星星跨日保留：`count` 为 0～999，`goal` 为 1～20；奖励文字 0～63 字节，
奖励图标 1～31 字节，留言 0～95 字节。省略可选星星字段会保留原值，空文字可清除奖励文字或留言。
固件保存总数；授予权限、每日上限和奖励约定由 Muse 管理。
`kid.mark` 用于远程更正，返回状态；实体按键事件由绿色键产生。时钟无效时，远程完成时间记为 0。

屏幕文字使用英文、ASCII 及已生成的弯引号、破折号、角度符号、省略号等标点。
Muse 可将中文安排转成简短英文。图标支持 `wake`、`sun`、`toothbrush`、`shirt`、
`breakfast`、`bowl`、`backpack`、`school`、`homework`、`book`、`swim`、`piano`、
`music`、`bath`、`bed`、`sleep`、`play`、`park`、`car`、`pickup`、`star`、`gift`、`reward`。
未知条目图标显示步骤文字，未知奖励图标显示“Gift”及奖励文字。图案使用面板六种原生墨色着色。

安排、完成情况、星星和留言通过带版本及校验的数据块保存在 NVS，重启后恢复。
有效 RTC 可恢复时间，联网校时或 `clock.set` 会更新 RTC；夏令时变化时由 Muse 更新本地时差。
指令会报告存储失败，按键确认若只保存在内存，会在一分钟内重试存储。
共享事件服务使用有限内存队列；主人收到通知还取决于联网及供电情况，`events.poll` 可补查
保留的最近三十二条通知。按键确认代表孩子按下了完成键。

## 验证

在 `esp32/` 执行：

```sh
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf6.0_py3.14_env
python3 -m unittest tests.test_kid_board tests.test_care_board tests.test_epaper_cards tests.test_gadget_events
CARD_PREVIEW_DIR=/tmp/card-preview python3 -m unittest tests.test_kid_board
tools/board.sh reterminal-e1002 build
tools/board.sh reterminal-e1001 build
```

预览由实际固件绘图函数生成，使用六种准确 RGB 墨色，输出 `kid_morning`、
`kid_afternoon_changed`、`kid_done`、`kid_stars_goal` 的 PPM 与 PNG。
[VALIDATION.md](VALIDATION.md) 记录本次版本、构建体积、主机测试和待完成的实机验证。
