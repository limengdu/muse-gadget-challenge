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

# Gadget 共享基础

E1001、E1002 的配置已启用 `HOMEHUB_GADGET_EVENTS`、`HOMEHUB_GADGET_CLOCK` 和
`HOMEHUB_EPAPER_CARDS`。其他项目按需选择，固件使用 ESP-IDF v6.0.1。

## 设备事件

应用启动后，各任务可调用 `gadget_events_post(source, summary, data)`。
函数立即尝试入队并复制参数，返回是否成功。来源最多 47 字节，摘要最多 511 字节，
紧凑 JSON 数据最多 1024 字节。消息格式为 `[source event] summary`，有数据时另起
一行附上 JSON。内存分配失败、输入无效或队列锁正忙时返回 false，生产者可稍后重试。

后台任务逐条发送：先订阅 `/chat/subscribe`，再向 `/chat/stream` 发 JSON，使用
发送确认中的消息编号关联回复。最近一轮回复最多保留 600 字节的完整 UTF-8 字符，
通过 `gadget_events_reply()` 读取。尚未提交的请求按 1～30 秒逐步延长间隔重试；
已提交的消息最多等 90 秒接收回复，回复不可用时记录结果并继续处理下一条事件。

待发队列保存 16 条，正在处理的一轮单独持有。队列满时移除最旧的待发事件并增加
`dropped`。另一个环形记录保留最近 32 条已接收事件，也包含从待发队列移除的条目。
`events.poll` 的可选参数 `since` 是非负整数，返回 `events`、`last_seq` 和 `dropped`。
每条记录含 `seq`、`time`、`time_basis`（`epoch` 或 `uptime`）、`source`、`summary`
及可选的 `data`。序号在本次开机期间持续增加，队列和记录保存在内存中。

Link 控制台输入 `>event=Ready for school` 可发送来源为 `console` 的事件。
进度以 `@event` JSON 输出，状态为 `queued`、`sent`、`reply` 或 `error`；`reply` 的
`text` 字段包含回复。回复正文走该控制台路径。
`sdk_token_console_register_handler()` 在启动期间注册其他行处理器，SDK token
处理器优先执行。

## 本地时间

无线网络获得 IP 后，通过 `esp_netif_sntp` 向 `pool.ntp.org` 校时。
有效年份为 2024～2099。`gadget_clock_set(utc_epoch, utc_offset_min)` 也可设置 UTC，
并通过 `config_store` 的 `utc_offset_min` 键保存本地时差，范围为 -840～840 分钟。
Muse 通过 `clock.set` 传入这两个整数参数，独立后台任务完成时钟和 NVS 更新。

页面可调用 `gadget_clock_valid()`、`gadget_clock_epoch()`、
`gadget_clock_local(&tm)` 和 `gadget_clock_minutes_of_day()`。
最后一个函数在时间无效时返回 -1。本地时间等于 UTC 加固定时差；项目按需更新夏令时偏移。

## 卡片页面

`epaper_cards_canvas()` 包装原生面板字节：E1001 为高位优先的 1 位黑白格式；
E1002 为高半字节优先的 4 位墨色编号 0、1、2、3、5、6。
E1001 将所有非白颜色显示为黑色。接口提供可带圆角的矩形、横竖线、比例字体、文字测量、
自动换行与末行省略号、按名称查找的图标、勾选框和墨色反转。
文本坐标以行顶端为准，字体自带基线。文字尺寸为 28、40、60、96，图标尺寸为 64、112。
`epaper_cards_text_box()` 返回行数及截断状态。

调用 `epaper_cards_show(render, ctx)` 发布页面，调用
`epaper_cards_request_redraw()` 请求重绘。二者立即尝试排队，返回 false 时可重试；由现有电子纸任务调用
绘图回调并完成一次全屏刷新。回调直接写面板缓冲区。回调代码与上下文应存活至所有待处理
回调结束，共享页面数据需同步访问；可使用静态应用模型，或在回调中取得模型快照。

刷新期间的多次重绘请求合并为随后一次绘制。页面代号用于丢弃绘制过程中已被替换的页面；
面板锁使卡片、图片、状态刷新串行进行。显示卡片或图片时保留状态变化，待返回状态页后显示。
`display.draw_url`、`display.draw_file` 和 `display.show_animation` 通过现有路径替换卡片。
关闭卡片选项时使用原有状态和图片实现。

## 资源与主机预览

在仓库根目录，用本地 Pillow 环境生成：

```sh
.venv/bin/python esp32/tools/gen_card_assets.py
```

文字来自 `components/muse/eink/fonts/montserrat.ttf`，采用 SIL OFL 1.1。
长短破折号和弯引号由字体原有的连字符、逗号字形组合而成。图标输入位于仓库根目录的
`.cache/fonts/MaterialSymbolsRounded.ttf`，同目录包含 `.codepoints` 和
`MATERIAL-LICENSE`，采用 Apache-2.0。生成器按轴名称选择填充 1、字形修正 0、光学尺寸
48、字重 500；扩展排序后的 `ICONS` 列表即可增加图标。

生成的常量数组存放在闪存中，保留字形与图标边框、比例字距和基线。
`main/card_assets_manifest.json` 记录输入校验值及含元数据、名称的 ESP32 资源字节数，
生成器检查总量在 160 KiB 以内。完整第三方许可见 `main/card_assets_licenses/`。

在 `esp32/` 运行：

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
CARD_PREVIEW_DIR=/tmp/card-preview python3 -m unittest tests.test_epaper_cards
```

第二条命令生成 800×480 的 `e1001_sample.pgm` 和 `e1002_sample.ppm`。
主机测试覆盖事件发送、时差保存、像素打包、排版、边界裁切及页面替换。
面板刷新时间、实际墨色和 Muse 实时收发通过设备验证。
