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

# Home Eyes：让 Muse 帮你看看家里

[English](README.md)

出门后想看看灶台、门口、狗碗或打印机，也想给家里人留句话？
Home Eyes 利用 SenseCAP Watcher 板载摄像头、412×412 圆形触摸屏、滚轮、
扬声器和麦克风，把这些事情交给你自己的 Muse。

## 准备设备

使用本仓库和 **ESP-IDF v6.0.1**，安装过程见[固件指南](../../esp32/AGENTS.md)。
将 Watcher 固定朝向目标，长期看守建议接 USB 电源，使用现有 Himax 相机固件即可。

**首次刷机前**，将完整闪存和这台设备独有的工厂分区备份到已被 Git 忽略的目录。
在 esp32/ 中执行，PORT 换成 Watcher 的控制台串口：

~~~sh
mkdir -p ../backups
python tools/muse/paced_esptool.py --chip esp32s3 -p PORT read-flash 0 ALL ../backups/watcher-factory.bin
python tools/muse/paced_esptool.py --chip esp32s3 -p PORT read-flash 0x9000 0x32000 ../backups/watcher-nvsfactory.bin
tools/muse/board.sh build watcher
idf.py -B build-muse-sensecap-watcher menuconfig
tools/muse/board.sh build watcher
tools/muse/board.sh flash watcher
~~~

[设备指南](../../esp32/devices/README.md#watcher-camera)介绍了分段发送的 USB 工具和
nvsfactory 备份。将备份与对应设备一起保管。
在 menuconfig 的 Device SDK 设置中填写自己的 SDK token，对应项为
CONFIG_GADGET_SDK_TOKEN；它保存在已被 Git 忽略的私有构建目录中。

Watcher 配置已启用 CONFIG_MUSE_WATCHER_CAMERA、CONFIG_MUSE_HOME_EYES、
CONFIG_HOMEHUB_GADGET_EVENTS 和 CONFIG_HOMEHUB_GADGET_CLOCK。
已有构建目录可通过 menuconfig 启用这些选项，新构建目录自动加载配置。

打开手机上的 Muse，添加附近的 Watcher、设置家中 Wi-Fi，并按提示按下滚轮确认配对。
Watcher 保持连接家中 Wi-Fi，手机可以在外面使用。
将 [Home Eyes 技能](SKILL.md)内容或仓库中的文件链接发给 Muse，请它安装。

## 可以这样说

- 「看看厨房，灶台看起来关了吗？」
- 「每两分钟看看打印机，到今晚八点为止，留意打印件是否脱离底板。」
- 「用低灵敏度看着门口，有包裹到了吗？」
- 「告诉孩子们，我二十分钟后到家，署名妈妈。」
- 「现在看守哪里？」或「停止看守。」

说清可见目标和具体问题。开始看守前，让 Muse 先看一次当前画面。
如需发现故障后暂停打印机，明确指定设备和故障条件，搭配已有
[Moonraker 技能](../../skills/gadget-moonraker-3d-printers/SKILL.md)使用。

## 命令

| 命令 | 参数和效果 |
|---|---|
| camera.capture | 现有拍照命令，向 Muse 返回 JPEG，适合临时看一眼。 |
| watch.start | 必填 name（1～48 个 UTF-8 字节）、question（1～192）；可选 interval_min（整数 2～120，默认 10）、sensitivity（low/medium/high，默认 medium）、until（未来 UTC 时间戳，单位秒）。返回看守状态。 |
| watch.stop | 停止看守，现有卡片继续等待确认。 |
| watch.status | 返回 active、awaiting_verdict、name、question、interval_min、sensitivity、until、next_check_s、message_unseen、alert_unacknowledged、verdict、last_error、saved。 |
| watch.verdict | 必填 text（1～480 字节）、alert（布尔值），接收判断并更新比较基准。 |
| home.say | 必填 text（1～480 字节）、from（1～48 字节）；立即返回接收结果，稍后由家里人阅读。 |
| clock.set | 现有命令，用 utc_epoch 和 utc_offset_min 设置 UTC 时间和本地固定时差。 |
| events.poll | 现有命令，可用 since 序号查询最近事件。 |

设备通过 SNTP 校时。用 clock.set 设置当地时差，留言卡默认显示 UTC。
设置 until 需要有效时钟。saved=false 表示后台正在保存，再查 watch.status 可确认。
last_error 显示拍照或保存失败；后续成功拍照时清除拍照错误。

## 家里的人怎么操作

原有表情旁显示眼睛徽标、看守名称和下次检查倒计时。
普通判断以卡片覆盖表情，响一次轻提示音；警报增加红圈，每五秒响一次，直到按下滚轮。
留言显示发送人、正文和本地到达时间，时钟尚未同步时会有说明。
长文字可上下触摸滚动阅读。

卡片出现时，按一下滚轮确认，这次松开动作也由卡片处理。
警报优先显示，确认后显示留言；留言确认后显示尚未关闭的判断卡，随后回到 Muse 表情。
卡片关闭后恢复按住滚轮说话、双击预览或拍照，以及原有导航。
Muse 的回答以文字显示，设备使用提示音提醒。留言十分钟未读时再提醒一次。
同时支持一个看守和一条未读留言。更换看守前先完成待处理判断或停止看守；
上一条留言已确认、已读事件已成功排队后，可以接收新留言。

## 原理与边界

单次拍照沿用相机的现有供电逻辑，打开实时预览时使用当前预览画面。
芯片自带解码器先将 JPEG 缩小到八分之一，再生成 32×32 灰度小图，
经过 3×3 模糊、扣除整体亮度偏移后比较差异。
低、中、高灵敏度阈值分别为 18、10、5 个灰度级，实际场景仍需实机调整。

第一张图建立基准，画面变化后只发送一个事件，请 Muse 调用 camera.capture 和
watch.verdict。等待判断期间暂停检查，判断完成后以变化图更新基准并重新计时。
警报未确认时暂停检查。相机失败则等下一个间隔重试，并在 watch.status 中记录。
后台任务等到期限或状态变化时再工作。

配置、卡片和待确认状态保存在 NVS 中。照片、小图只在内存中。
重启后重新建立基准，待处理的判断会再次请求。
UTC 有效时恢复保存的检查期限和留言等待时间；UTC 无效时从新基准继续，
带 until 的看守等待校时。基础事件队列可能延迟、在重启时重复，或因离线积压丢弃，
可通过 watch.status 和 events.poll 查看状态。

屏幕、音频和处理器沿用电池休眠逻辑。为持续接收远程命令，Wi-Fi 保持省电连接，
取代长时间关闭 Wi-Fi 的断网休息。耗电和拍照、休眠时序需实机测量；
长期无人值守建议接 USB 电源。

Muse 请求的照片仅通过已配对的用户自己的 Muse 传输。
Home Eyes 不把照片写入闪存或 SD 卡，比较小图仅在内存中；
看守配置、留言和判断文字保存在设备上。账号侧保留方式由用户的 Muse 设置决定。
画面监测提供提醒线索，单张照片无法确认安全。

## 验证

在 esp32/ 中执行：

~~~sh
python3 -m unittest discover -s tests -p 'test_home_eyes.py'
python3 -m unittest discover -s tests -p 'test_*.py'
tools/muse/board.sh build watcher
~~~

主机测试覆盖噪声与曝光、阈值、间隔与到期、判断与警报确认、留言提醒、校验、
重启保存、事件重试，以及功能开关两种状态下的完整命令清单大小。
[模拟器](../../esp32/simulator/README.md)包含 412×412 圆屏的看守徽标、留言、
判断、警报和确认后返回表情场景。

实机还需检查：全新构建及固件大小、配对后的远程拍照和事件、
实时预览与定时检查、长文字滚动、滚轮确认、提示音和警报停止、
重启恢复、休眠时 Wi-Fi 可达性及两次拍照间的电池休眠。
编译、模拟器和实机结果分别记录。

构建、完整测试和待实机检查的详细记录见[验证报告](VERIFICATION.md)。
