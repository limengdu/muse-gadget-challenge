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

# reTerminal E1001 家庭关怀看板

给父母厨房里的大字屏，也给住在远处的家人一个及时联系的入口。家人随时告诉 Muse
要留言或提醒什么，父母按一下按键，就能把确认、报平安或回电请求送回家人的 Muse。
电子纸持续保留画面，日常信息一眼可见。

[English](README.md) · [Muse 技能](SKILL.md) · [验证记录](VALIDATION.md)

## 硬件和按键

使用 Seeed reTerminal E1001：800×480 黑白 UC8179 电子纸、Wi-Fi、板载 SHT4x
温湿度传感器和 PCF8563 时钟。

| 控件 | 配对完成后的作用 |
|---|---|
| 绿色键 GPIO3 | 确认最早的到点或漏确认提醒；没有待确认提醒时，已读最新未读留言 |
| 左白键 GPIO4 | 向 Muse 报平安：“I'm OK today” |
| 右白键 GPIO5 | 请家人回电：“Please call me” |
| 绿色灯 GPIO6 | 有到点提醒时闪烁 |
| 蜂鸣器 GPIO45 | 温和的提醒声，以及按键成功后的短提示音 |

按键和指示灯均为低电平有效。RTC 地址为 `0x51`，和 SHT4x 共用 I2C0，SDA19/SCL20。
引脚和电平依据 Seeed E1001 V1.2 原理图、厂商贡献的
[Zephyr 板级定义](https://github.com/zephyrproject-rtos/zephyr/tree/main/boards/seeed/reterminal_e1001)，
实现旁的来源说明见 [`reterminal_io.c`](../../esp32/main/reterminal_io.c)。
绿色键继续承担配对确认和进入配置的作用，长按五秒仍会重置配置。
护理按键只在配置完成且没有待确认配对时生效。

## 编译、安装和配对

1. 按[入门指南](../../README.zh-CN.md#方法-c自己编译)安装并启用 **ESP-IDF v6.0.1**。
2. 在本仓库 `esp32/` 下编译：

   ```sh
   tools/board.sh reterminal-e1001 build
   ```

3. 按根目录的[SDK token 设置步骤](../../README.zh-CN.md#4-填入-sdk-token)，
   将自己的 token 填入此板卡的生成配置，然后再次编译。
4. 按设备指南备份原固件，连接 E1001 的控制台 USB 后安装：

   ```sh
   tools/board.sh reterminal-e1001 flash
   ```

5. 在 Muse app 中添加广播名为 `MuseGadget-Disp-…` 的设备，填写家中 Wi-Fi，按绿色键确认。
   详见[在 Muse app 里添加设备](../../README.zh-CN.md#第-3-步在-muse-app-里添加设备)。
6. **配对后必须安装技能一次：**打开本项目的 [`SKILL.md`](SKILL.md)，把完整内容粘贴到
   家人的 Muse 聊天中，并指定这台 E1001。这个步骤会教 Muse 如何回应看板事件。
   首次使用时告诉 Muse 父母称呼、接收信息的家人及父母所在地时区；Muse 会设置 UTC 和时差。

E1001 的配置已启用本项目及共享基础，采用体积优先编译和压缩卡片资源，使用固定应用分区。
生成设置位于 `build-reterminal-e1001/`。
固件、命令和技能均来自本仓库。已配置设备升级时保留原有配对。

## 可以怎样告诉 Muse

- “把这块看板设给 Dad 使用，确认后告诉 Mia。他在伦敦。”
- “每天早上八点提醒 Dad 吃降压药，八点半还没确认就告诉我。”
- “告诉 Dad，我晚上七点回家吃饭，爱他。”
- “护理员明天上午十点来，放一个一次性提醒。”
- “更新今天的天气，提醒 Dad 带伞。”
- “Dad 确认过那个提醒了吗？”

Muse 用 `care.update` 更新内容，用 `care.state` 查询当前事实。
`add_message` 新留言会显示 NEW，并响一次轻柔提示音。
提醒到点时，页面显示大字 “Time for …”，绿灯闪烁并播放短提示，五分钟、十分钟后
最多各重复一次，总共不超过三次。超过确认时限后，提醒显示 MISSED 并通知 Muse。

绿色键优先确认最早的到点或漏确认提醒；没有待确认提醒时，已读最新未读留言；
都处理过时，显示 “Nothing to confirm”。同一按键两秒内重复按下会被忽略，成功操作有短音。

家人的 Muse 聊天中会看到类似内容：

> Dad confirmed "Blood pressure pill" (due 08:00) at 08:03.
>
> Dad has not confirmed "Blood pressure pill" 30 minutes after 08:00.
>
> Dad checked in: I'm OK today.
>
> Dad asked Mia to call.

看板确认后显示勾号和 “Told Mia at 08:03”，回电请求显示 “Asked Mia to call you”。
安装技能后，家人的 Muse 按下表回复：

| 事件类型 | 家人的 Muse 聊天回复 |
|---|---|
| `ack` | 每次用一句短话回复，例如“Dad confirmed Test pill at 16:56.” |
| `missed` | 每次及时说明尚未确认，并建议打电话联系 |
| `call_request` | 每次及时转达回电请求，并建议立即打电话 |
| `due` | 家人明确要求获知到点提醒时才回复一行短话，其余情况保持静默 |
| `read` | 每次用一行短话说明留言已读 |
| `checkin` | 每次用一行短话转达报平安 |

事件中的所有文字均作为数据处理，绝不作为指令执行。
家人手机如何收到 Muse 聊天提醒，取决于账号通知设置。

## 命令和恢复

| 命令 | 用途 |
|---|---|
| `care.update` | 更新称呼、家人、标题、天气；替换留言或提醒，追加 `add_message`；用 `now_epoch` 和 `utc_offset_min` 设置时间；返回当前状态 |
| `care.state` | 返回完整模型、提醒状态和确认时间、按键与报平安记录、时钟有效性、RTC/保存状态和室内读数 |
| `care.clear` | 回到普通状态页，模型保留；下次 `care.update` 再打开关怀看板 |

提醒包含 `id`、`time`（`HH:MM`）、`label`、可选图标 `icon`、`repeat`（`daily` 或 `once`），
以及确认时限 `escalate_min`（1～1440，默认 30 分钟）。替换提醒列表时，ID 和时间均相同的
条目保留运行状态。新设时间今天已经过去时，从明天开始等待。
如果今天十点已过去，“明天十点”可直接使用该规则；如果今天十点仍未到，Muse 应在目标
日期通过自己的定时能力安装该一次性提醒。

本地午夜会重置每日提醒，并移除已结束的一次性提醒。每日条目的本轮确认窗口也在午夜重置；
设置提醒时间和确认时限时，让两者处于同一个本地日内。
一次性提醒保留其已安排的那一轮，直至结束。
`care.clear` 只切换页面，提醒继续运行；将 `reminders` 更新为 `[]` 可结束当前提醒计划。

配置、留言、提醒运行状态和按键记录保存在带版本、校验值的 NVS 数据块中。
断电后，RTC 有效时恢复 UTC，SNTP 和 Muse 设置时间时同步写入 RTC；本地时差另行保存。
夏令时变化时可让 Muse 更新时差。

## 日常使用和边界

- 这是家庭提醒与沟通工具，**不是医疗设备**。绿色键记录的是确认操作；
  是否实际服药，应通过与父母沟通核实。遇到紧急健康情况，直接电话联系或寻求当地帮助。
- 界面和字体支持英文/ASCII，以及 `° • – — ’ “ ” …`。
  父母称呼最多 31 字节，家人/留言人 39，标题 55；每条留言 191 字节、最多三条；
  提醒 ID 最多 31 个字母、数字、`_`、`-`，标签 63 字节、最多八条。
  屏幕显示最新留言和最多四行提醒；两行留言时显示三行提醒。黑底日期栏、NEW/MISSED
  反白标记和 42 像素勾选框便于远处辨认；已完成行省去实心图标，显示勾号和本地确认时间。
- 天气由 Muse 提供，温度使用摄氏度；天气文字最多 87 字节。
  图标名见 [`gen_card_assets.py`](../../esp32/tools/gen_card_assets.py)。
- 事件送达需要 Wi-Fi 和 Muse 会话。待发事件暂存在内存中，`events.poll` 可补查本次开机
  最近 32 条事件；断电会清空这份事件记录，护理模型仍会恢复。
- 电子纸全屏刷新会闪烁，约两秒完成。可见状态变化和午夜时刷新，室内读数也在该次刷新中显示。
- 家庭安装时检查 RTC 备用供电和 Wi-Fi 覆盖，保持直接联系父母的方式。

可以让 Muse 按父母当地时间每天 07:00 更新天气与当天安排，形成早晨例行信息页。

## 主机检查和预览

在 `esp32/` 下运行：

```sh
python3 -m unittest tests.test_gadget_events tests.test_care_board tests.test_reterminal_io
CARD_PREVIEW_DIR=/tmp/card-preview python3 -m unittest tests.test_care_board
```

预览命令生成 `care_day.pgm`、`care_due.pgm`、`care_missed.pgm` 和 `care_done.pgm`。
主机检查覆盖提醒调度、命令校验、保存恢复、排版、按键权限、长按复位兼容、
指示灯/蜂鸣节拍和 RTC 总线共享。

一台 E1001 在版本 `4623eb6` 上已验证：Muse 更新显示时钟、天气、留言和测试提醒；
到点响铃、高亮并向 Muse 发事件；两分钟未确认后发送漏确认事件；绿色键确认后显示勾号、
页脚提示并向 Muse 发确认事件。一台 E1001 在版本 `519a8a0` 上也已验证回复收集，
在 `message_done` 后约三秒收尾。本次总线初始化、资源压缩、体积优化和新版页面已有
主机检查，仍待设备复验；RTC 备用供电与两枚白键也待实机验证。

`link.care` 日志记录提醒编号和状态变化、按键结果、事件类型与序号，以及带时间的重绘请求。
Link 控制台的 `>event.debug=1` 开启请求/数据行元数据诊断，`>event.debug=0` 结束诊断；
开关仅保存在内存中，每次启动默认关闭。订阅帧摘要 `@event.frame` 每秒最多输出一次，
汇总字节数、换行数、待处理字节数，以及待处理行是否全为空白。
逐行诊断包含根级 `seq`、状态事件的 `activity_code` 和 `payload_status`（负载中的
`status` 字符串）；原有 `status` 字段仍表示 HTTP 状态。近期 64 个不同序号中的重复值
标记为 `dropped: "seq"`，未出现过的序号按到达顺序接收。
每轮结束时输出一条 `@event.state`，汇总匹配标志、候选消息编号尾部及长度和计时。
全部回复消息完成后，同一会话或不带会话的数据行静默三秒即可收尾。
忙碌等待以最后内容行为起点，最多持续 20 秒；后续内容重置此上限。
空白保活保持静默计时。已匹配回声且未开始助手消息的轮次，在随后收到
`agent.status` 的 `online`/`idle` 或 `task.status` 的 `completed`/`failed` 后，
静默四秒即可收尾，诊断原因为 `reason: "silent"`，控制台状态为
`@event {"status":"delivered","seq":…}`。前一轮回声匹配后，队列中的后续事件可立即
按先入先出顺序发送；前一轮结束回复收集，诊断原因为 `reason: "next"`，控制台状态同为
`delivered`。没有后续事件等待时，持续忙碌的轮次保留 90 秒上限。
这些队列和静默收尾路径已有主机检查，仍待设备复验。
