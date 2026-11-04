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

# 冰箱便签 Fridge Notes

[English](README.md)

适合一家人共同使用的购物、家务和待办清单。做饭时发现鸡蛋没了，双手又忙着，
就按住冰箱上 reTerminal Sticky 的 AI 键告诉 Muse；出门采购的人随时问 Muse 还缺什么。
家里的人也能直接点屏幕打勾。Sticky 用电池睡眠时，Muse 可以读取之前同步到个人 VM 的清单。

## 能做什么

- 最多四张清单，例如购物、家务、待办、便签，每张最多二十项。每项包含固定数字编号、
  文字、完成状态、可选负责人和 UTC 添加时间。
- 清单保存在设备的 NVS 存储中，重启后恢复。写入失败时保留原清单，并提示重试。
- FreeInk 主页提供清单标签、大复选框、已完成项目的灰色删除线、Clear done 按钮、
  电池百分比和同步状态。
- 修改后等待约三秒，将所有清单同步到个人 Muse VM 主目录下的
  `gadget/fridge-notes/lists.json`。离线时三十秒后重试。屏幕操作还会向自己的 Muse
  聊天发送简短的 `[fridge-notes event]` 消息，连续操作合并成一条。
- AI 键继续使用现有 Muse 语音便签流程，回复以文字显示，读完后回到清单。

## 硬件与验证范围

使用 **Seeed reTerminal Sticky 完整 Muse 界面配置**：480×800 竖屏 SSD1677 电子墨水屏、
GT911 触摸、四级灰度、PDM 麦克风及电量计。AI 键录制语音并确认配对，Up/Down 唤醒屏幕。
设备没有扬声器，回复显示为文字。项目使用现有板级驱动的引脚和外设。
参见 [Sticky 硬件及验证说明](../../esp32/devices/README.md) 和 [固件操作说明](../../esp32/AGENTS.md)。

板级支持与本应用均处于实验阶段。主机测试覆盖清单、保存、VM 写入协议和 FreeInk 导航；
本应用的真实屏幕响应、VM 同步、语音转清单、睡眠唤醒及电池表现仍需实机验证。
详见 [验证记录](VALIDATION.md)。

## 编译、配网与安装技能

1. 安装 **ESP-IDF v6.0.1**，打开本仓库并运行它的 `export.sh`。进入 `esp32/`：

   ```sh
   tools/board.sh reterminal-sticky build
   ```

   `devices/sdkconfig.muse` 与 `devices/sdkconfig.reterminal-sticky` 会启用
   `MUSE_STICKY_NOTES`，并自动启用设备事件和时钟。已有构建配置会保留之前的设置：
   运行 `idf.py -B build-reterminal-sticky menuconfig`，在 ESP32 Device SDK 中启用
   Fridge Notes，再编译。此选项需要选择 Sticky 板及启用 VM 请求功能。

2. 获取自己的 Muse SDK token，通过 `idf.py -B build-reterminal-sticky menuconfig`
   设置 gadget SDK token，保存并重新编译。生成的配置位于
   `build-reterminal-sticky/sdkconfig`，Git 已忽略此目录。妥善保存 token 与固件文件。
   第一次刷机前，按 [凭据与备份说明](../../AGENTS.md#credentials-and-backups) 备份设备。

3. 用已备份设备的控制台 USB 端口刷机：

   ```sh
   tools/board.sh reterminal-sticky flash PORT
   ```

   将 `PORT` 替换成该设备的 CH343 控制台端口。打开手机 Muse，选择广播中的设备，
   填入 Wi-Fi，并按 AI 键确认配对。设备设置页仍可用屏幕键盘输入 Wi-Fi 信息。

4. 将完整的 [SKILL.md](SKILL.md) 复制到自己的 Muse 聊天中，请 Muse 安装，步骤同
   [仓库技能安装说明](../../skills/README.md)。然后说：

   > 在冰箱上建购物、家务、待办、便签四张清单。

   Muse 使用 `notes.set`，清单标识分别为 `shopping`、`chores`、`todo`、`notes`，
   显示标题可以使用中文。第一张清单建立后，清单页成为主页，也可以只建需要的几张。

5. 连接 Wi-Fi 后等待自动校时。请 Muse 用 `clock.set` 设置当前 UTC 时间戳和本地
   时区相对 UTC 的分钟偏移，供同步时间显示使用；夏令时变化时更新偏移。
   尚未取得有效时间时，新增项目的 `added_at` 为 `0`。

## 可以怎么说

- “鸡蛋没了，再买瓶酱油。”→ 拆成两项购物清单。
- “买两盒牛奶和五百克番茄。”→ 数量和单位保留在文字中。
- “把周六大扫除加到家务里，交给 Leo。”→ 保存任务文字和 `who: "Leo"`。
- “冰箱清单还有什么没做？”→ 只读未完成项，带上数量和负责人。
- “鸡蛋买好了。”→ 先查最新编号，再明确设置 `done: true`。
- “把做完的家务清掉。”→ 清理家务清单中的完成项。
- “每周日晚上提醒我们看看剩下的任务。”→ 可选 Muse 例行安排；先约定时间、时区和清理范围。

设备在线时，Muse 优先调用设备命令；睡眠时，读取 VM 文件，并说明这是最近一次成功同步的记录。
需要添加或修改时，等设备恢复在线再写入。VM 文件由设备负责更新。

## 家里怎么用

按住 AI 键说话，松开后看简短确认。点一行即可勾选或取消；点顶部标签或左右滑动切换清单。
在最后一张清单继续向左滑可进入 Settings；从设置页向右返回。
从第一张清单向右滑，或点击 Muse 按钮，可看表情页；再向左滑回清单。
上下滑动或点 Previous/Next，可浏览长清单的全部项目。Clear done 清除当前清单已打勾的行。
Up/Down 唤醒设备，正常电池睡眠期间屏幕继续保留原画面。

“Synced hh:mm” 是最近一次完整上传的本地时间；星号表示还有新修改待上传。
“Sync pending” 表示等待同步，“Synced --:--” 表示同步已成功但当时还没有有效的本地时间。
“Save failed: retry” 表示保存失败、旧数据仍保留；“Busy: tap again” 表示操作队列忙，稍后再点。
屏幕截短的文字可以让 Muse 读取完整内容。

## 命令与限制

成功结果包含 `ok: true`，以及带 `version`、`revision`、`next_id`、`lists` 的 `payload`。
修改命令返回受影响的一张清单；`notes.get` 返回全部或指定清单。
VM 文件采用相同结构并始终包含全部清单。参数和示例见 [SKILL.md](SKILL.md)。

- 清单标识为 1–23 字节的小写英文字母、数字或连字符；标题和负责人为 1–31 字节；
  条目文字为 1–127 字节的单行 UTF-8 文本。中文通常一字占三字节。
  四张清单、每张二十项的限制统一校验，一次请求中的无效条目会使整次修改失败。
- `notes.set` 替换一张清单。条目编号全局唯一、重启后继续递增。
  修改完成状态或删除前先取最新编号；明确传 `done` 可安全重试相同完成状态。
- 清单与配对、设置共用现有 NVS 分区，写满时会拒绝修改。保持文字简洁，分区表沿用原配置。
- 同步为设备到 VM 的单向镜像。大文件分多次写入，上传期间可能暂时读到不完整 JSON，
  稍后重新读取即可。中断后从覆盖写入重新开始，所有分块成功后才更新同步状态。
  离线修改尚未上传时，VM 记录可能落后。
- 触摸事件会重试至共享事件队列接受；待发事件存在内存中，突然断电可能丢失事件消息，
  已保存的清单仍留在 NVS。消息进入自己的 Muse 聊天，冰箱前的人也能看到清单；
  适合记录日常家务与采购内容。

## 主机验证

在 `esp32/` 中，使用 Sticky 构建提供的固定版本 FreeInk 源码：

```sh
FREEINK_SOURCE_DIR=$PWD/managed_components/freeink-sdk python3 -m unittest discover -s tests -p 'test_*.py'
```

只测本应用时，将筛选改为 `-p 'test_sticky_notes*.py'`。
测试使用真实 cJSON、应用生产代码及固定版本 FreeInk 的触摸路由，硬件通过测试替身模拟。
