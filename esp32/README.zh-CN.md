<!--
Copyright (c) Meta Platforms, Inc. and affiliates.

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

# ESP32 设备 SDK

> 本文是 [README.md](README.md) 的中文译本。

把这份开源固件烧录到任何兼容 ESP32 的开发板上，就能让 Muse 连上你家的 Wi-Fi。在带有家庭网络隧道的板子上，Muse 可以访问你已经拥有的设备，以及任何你用本地 HTTP API 搭出来的东西。

然后你可以接着改：加一块屏幕、一个按键，或者支持一块我们还没试过的板子，做出你自己的 Muse 设备。

> **说明：** 由极客写给极客，纯粹为了好玩。烧录自定义固件可能让板子变砖，也会使保修作废。风险自负！

## 你需要准备什么

- **一块 ESP32 开发板。** 最快的入门方式是 **ESP32-C5 DevKitC-1**。它自带状态灯和 BOOT 按键，插上就能用。其他已经能用的板子列在[下方](#开发板)。
- **一根能传数据的 USB 线**，不能只供电。
- **一台电脑**，系统是 macOS 或 Linux。
- **一个 SDK token**，从 [gadgets.muse.ai](https://gadgets.muse.ai/settings/sdk-tokens) 获取（账户 > SDK tokens）。每台设备配对都需要一个，包括你自己做的。使用前先阅读 [Gadget SDK 条款](https://gadgets.muse.ai/sdk-terms)。
- **手机上的 Muse 应用**，固件烧录完成后用它来设置设备。

## 用 Muse Code 开始

最快的做法是让 [Muse Code](https://developer.meta.com/ai/lp/muse-code/) 来做。先安装：

```sh
curl -fsSL https://dev.meta.ai/install.sh | sh
```

然后插上开发板，在这个目录里启动 Muse Code：

```sh
git clone https://github.com/facebookincubator/muse-gadget-sdk
cd muse-gadget-sdk/esp32
muse --disable-sandbox
```

第一次运行时，Muse Code 会让你信任这个工作区并登录。
`--disable-sandbox` 让它能访问开发板的 USB 串口，并下载 ESP-IDF 工具链。每条命令执行前仍需要你批准。然后对它说：

> 给这块 ESP32-C5 DevKitC-1 编译固件并烧录。

Muse Code 会读 [`AGENTS.md`](AGENTS.md)。里面有配置工具链、按你的板子编译、烧录，以及读取日志所需的全部步骤。接下来可以继续说：

> 看着串口日志，准备好配对时告诉我。

> 我有一块 Waveshare ESP32-S3 AMOLED 板。给它做界面。

> 给我的板子加支持。它是 ESP32-S3，16 MB 闪存，按键在 GPIO 0，没有 PSRAM。

> 把状态灯亮度减半。

其他编程助手也可以。任何会读 `AGENTS.md` 的助手都能从这个仓库编译和烧录。烧录需要访问开发板的 USB 串口，所以如果你的助手跑在沙箱里，让烧录和监视命令在沙箱外执行。

## 或者自己动手

### 1. 安装 ESP-IDF v6.0.1

这份固件用乐鑫（Espressif）的 ESP-IDF 编译，版本是 **6.0.1**。其他版本不受支持。如果已经装好，可以跳过这一步。

在 macOS 上，先用 `brew install cmake ninja dfu-util python3` 装好前置依赖。在 Linux 上，按
[乐鑫的前置依赖说明](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32c5/get-started/linux-macos-setup.html) 操作。
然后：

```sh
git clone -b v6.0.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6
~/esp/esp-idf-v6/install.sh esp32c5,esp32s3,esp32c6,esp32
. ~/esp/esp-idf-v6/export.sh
```

每开一个新的编译终端，都要再执行最后一行。

### 2. 编译

在这个目录里，先设置 SDK token，再编译。菜单里的路径是英文的：`ESP32 Device SDK > Muse Gadgets SDK token`。

```sh
idf.py menuconfig   # ESP32 Device SDK > Muse Gadgets SDK token
idf.py build
```

这会为 ESP32-C5 DevKitC-1 编译。固件生成在 `build/muse-gadget.bin`。

没带 token 编译的固件，之后也能通过串口补上：在板子的串口控制台发送一行
`>sdk_token=mgst_…`，它会把 token 存进 NVS，回复 `@sdk_token ok` 并带着它重启。
存下来的 token 优先于编译时写入的。[网页烧录器](../web-flasher/README.md)在安装完成后会自动帮你做这一步。

Muse 只在 gadget 配对时记录它的 SDK token。如果你给已配对的 gadget 换了 token，或者作废了它配对时用的那个，
就要重新配对：按住按钮 5 秒，再在 Muse app 里配对。在那之前它一直用配对时的 token，
那个 token 一旦作废，它就连不上 Muse。gadget 配对用的 token 和当前的不一致时，开机日志会给出警告。

### 3. 烧录

插上开发板并烧录：

```sh
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

把端口换成你这块板子的。在 macOS 上，`ls /dev/cu.usb*` 会列出已连接的端口；在 Linux 上，查找 `/dev/ttyACM*` 或 `/dev/ttyUSB*`。监视器会显示设备日志；按 `Ctrl-]` 退出。如果烧录连不上，按住 **BOOT**，点一下 **RESET**，松开 **BOOT**，再试一次。

重新烧录会保留配对和 Wi-Fi 设置。如果要彻底从头来过，先运行 `idf.py -p PORT erase-flash`。

### 4. 用 Muse 完成设置

烧录完成后，状态灯会以 **橙色** 慢慢亮起再暗下去：设备已准备好设置。在 Muse 应用里打开 **设置 > 设备 > 开发者模式**，然后添加设备（**设置 > 设备 > 添加设备**，右上角的 **+** 图标）。它会显示为 `MuseGadget-XXXXXX`。当灯以 **蓝色** 慢慢亮起再暗下去时，按下 **BOOT** 按键，确认是你本人在操作。Muse 连上后，灯会变成 **绿色**。

| 灯 | 含义 |
|---|---|
| 橙色，慢慢亮暗 | 可以开始设置 |
| 蓝色，慢慢亮暗 | 按下按键确认配对 |
| 蓝色 | 正在加入 Wi-Fi 并连接 Muse |
| 绿色 | 已连接 |
| 黄色，闪烁 | 正在重连 |
| 紫色 | 尚未配对 |
| 红色，闪烁 | 出了问题：查看日志 |

要重置设备并重新设置，按住按键 5 秒。

在实验性的 EchoEar-2ST 上，按屏幕来确认配对；配对之后，按住屏幕上的麦克风来录音，松开即发送。操作方式和验证范围见 [EchoEar 设置](devices/ostb-echoear-2st.md)。

配对需要在设备上明确按一次，每次设置都会建立一次新的加密会话。因为这些是社区设备，配对没有厂商校验，也无法阻止有人在网络中间截获通信。请在你信任的网络上完成设置。

## 开发板

标了界面的板子会跑完整的屏幕界面：会动的头像、按住说话，以及设置。音频和图片支持因板而异，请看 [`devices/`](devices) 里的功能表。其余板子用一盏灯、一圈灯环，或一块简单的状态屏来显示状态。

| 开发板 | 你会得到什么 | 怎么编译 |
|---|---|---|
| ESP32-C5 DevKitC-1 | 状态灯和按键 | `idf.py build` |
| ESP32-C6 devkit without PSRAM | 状态灯和按键 | `tools/board.sh c6-nopsram build` |
| Espressif ESP32-S3-DevKitC-1 | 状态灯和按键 | `tools/board.sh espressif-s3-devkitc-1 build` |
| ideaspark ESP32 with 1.9" display | 屏幕显示状态，可显示图片 | `tools/board.sh ideaspark build` |
| Waveshare ESP32-C6-LCD-1.47 | 屏幕显示状态，可显示图片 | `tools/board.sh waveshare-c6-lcd-147 build` |
| Seeed SenseCAP Indicator | 4 英寸屏幕显示状态，可显示图片 | `tools/board.sh sensecap-indicator build` |
| Waveshare ESP32-S3-1.54inch-ePaper V2 | 1.54 英寸电子纸显示状态，黑白 | `tools/board.sh waveshare-s3-epaper-154 build` |
| Seeed reTerminal E1001 | 7.5 英寸电子纸显示状态，黑白图片 | `tools/board.sh reterminal-e1001 build` |
| Seeed reTerminal E1002 | 7.3 英寸电子纸显示状态，六色图片 | `tools/board.sh reterminal-e1002 build` |
| Seeed reTerminal Sticky（实验性） | 基于 FreeInk 的 3.97 英寸竖屏电子纸界面、触摸、PDM 语音输入、电池、SD 卡和 SHT40 | `tools/board.sh reterminal-sticky build` |
| Seeed reTerminal Sticky Link 状态屏（实验性） | 电子纸状态画面和 SHT40，无完整 Muse 界面 | `tools/board.sh reterminal-sticky-link build` |
| Home Assistant Voice Preview Edition | LED 灯环显示状态，按住说话，音量旋钮 | `tools/board.sh home-assistant-voice build` |
| Seeed reSpeaker Lite with XIAO ESP32-S3 (experimental) | 单颗 RGB LED，BOOT 按住说话；需要 16 kHz 的 XMOS I2S | [设置](devices/seeed-respeaker-lite.md) |
| Waveshare ESP32-S3-Touch-AMOLED-1.75C | 界面、按住说话、设置、图片 | 见 [`AGENTS.md`](AGENTS.md#boards-with-the-full-ui-by-hand) |
| Waveshare ESP32-S3-Touch-AMOLED-1.75 | 界面、按住说话、设置、图片 | 见 [`AGENTS.md`](AGENTS.md#boards-with-the-full-ui-by-hand) |
| Waveshare ESP32-S3-Touch-AMOLED-2.16 | 界面、按住说话、设置、图片 | 见 [`AGENTS.md`](AGENTS.md#boards-with-the-full-ui-by-hand) |
| OSTB EchoEar-2ST (experimental) | 界面、触摸麦克风和配对确认、本地音频 | [EchoEar 设置](devices/ostb-echoear-2st.md) |
| Espressif ESP32-S3-BOX-3 | 界面、触摸、按住说话、设置、图片 | [BOX-3 设置](devices/esp32-s3-box-3.md) |
| AIPI Lite | 界面、按住说话、双键菜单、图片 | 见 [`AGENTS.md`](AGENTS.md#boards-with-the-full-ui-by-hand) |
| Waveshare ESP32-C6-Touch-AMOLED-1.8 | 界面、按住说话并显示文字回复 | 见 [`AGENTS.md`](AGENTS.md#boards-with-the-full-ui-by-hand) |
| Waveshare ESP32-C6-Touch-AMOLED-2.06 | 界面、触摸、按住说话并显示文字回复 | `tools/muse/board.sh build c6-206` |
| Seeed SenseCAP Watcher | 界面、按住说话、设置、图片 | 见 [`AGENTS.md`](AGENTS.md#boards-with-the-full-ui-by-hand) |
| M5Stack Cardputer ADV (experimental) | 界面、GO/Space 按住说话并显示文字回复，Esc/Enter/方向键控制菜单 | `tools/muse/board.sh build cardputer-adv` |
| M5Stack StickS3 | 界面、按住说话、双键菜单、图片 | 见 [`AGENTS.md`](AGENTS.md#boards-with-the-full-ui-by-hand) |
| M5Stack StopWatch | 界面、按住说话、设置、图片 | 见 [`AGENTS.md`](AGENTS.md#boards-with-the-full-ui-by-hand) |
| [Seeed XIAO ESP32-S3 1.47 英寸触摸屏](devices/seeed-xiao-s3-147.zh-CN.md) | 界面、USR1 按住说话、触摸、设置、图片；PDM 麦克风，无喇叭 | `tools/muse/board.sh build xiao147` |
| Seeed XIAO AMOLED Display，2.05" 屏（实验性） | 界面、ENTER 按住说话、触摸、设置、图片 | `tools/muse/board.sh build xiao-amoled-205` |
| Seeed XIAO AMOLED Display，1.75" 圆屏（实验性） | 界面、ENTER 按住说话、触摸、设置、图片 | `tools/muse/board.sh build xiao-amoled-175` |
| Seeed XIAO AMOLED Display，2.16" 方屏（实验性） | 界面、ENTER 按住说话、触摸、设置、图片 | `tools/muse/board.sh build xiao-amoled-216` |
| M5Stack CoreS3 | 界面、触摸、PWR 按住说话、设置、图片 | 见 [`AGENTS.md`](AGENTS.md#boards-with-the-full-ui-by-hand) |
| Guition JC3248W535 | 界面、触摸、设置、图片、扬声器；外接 I2S 麦克风后可按住说话 | `tools/muse/board.sh build jc3248w535` |
| Waveshare ESP32-S3-Touch-LCD-7 | 界面、触摸、设置、图片；没有内置音频 | `tools/muse/board.sh build lcd7` |
| Waveshare ESP32-S3-Touch-LCD-1.28 | 界面、触摸、设置、图片；没有内置音频 | `tools/muse/board.sh build lcd128` |
| M5Stack StickC Plus2 | 界面、按住说话、双键菜单、图片 | 见 [`AGENTS.md`](AGENTS.md#boards-with-the-full-ui-by-hand) |
| M5Stack Core2 (v1.0) | 界面、触摸条上按住说话、触摸菜单、图片 | `tools/muse/board.sh build core2` |
| Freenove FNK0104B | 界面、触摸、BOOT 按住说话、设置、图片 | `tools/muse/board.sh build fnk0104b` |
| VN ESP32-S3 1.83-inch NV3023 | 界面、BOOT 按住说话、Vol+/Vol- 菜单、图片 | `tools/muse/board.sh build vn183` |
| FoloToy AI Passport (experimental) | 界面、按住说话并显示文字回复、三键菜单 | `tools/muse/board.sh build ai-passport` |

每块板的硬件、功能，以及哪里能买到，见 [`devices/`](devices)。

`tools/board.sh BOARD [build|flash|monitor|flash-monitor] [PORT]` 会在各自的 `build-<board>` 目录里，用对应的芯片和设置编译每块板。支持图片的板子可以显示 Muse 发来的图片：`tools/image_for_display.py` 会按屏幕尺寸准备一张图。

没有 PSRAM（板上的额外内存）的板子，比如经典 ESP32 和 ESP32-C6，不会跑家庭网络隧道，因为隧道需要的内存比它们拥有的更多。Muse 仍然可以连上这台设备并控制它。

## 功耗测量

从 [功耗分析](docs/power/README.md) 开始。使用 PPK2 之前先读 [PPK2 规则](docs/power/ppk2.md)；SenseCAP Watcher 有自己的 [不经过 USB 的步骤](docs/power/sensecap-watcher.md)。可选启用的功耗测试镜像会在拔掉 USB 之后，自动按时间顺序逐个测外设；一个 PPK2 进程会在重新连接期间保持供电不断。打开输出之前，必须先把电池隔离开，并防止 USB 充电器把电倒灌回去。这些诊断数字不是量产应用的功耗数据。

要测量另一块板或另一台设备，按 [移植步骤](docs/power/README.md) 来：凡是 PPK2 能供电的，都做带标注的 PPK2 采集；或者用与具体板型无关的 `CONFIG_MUSE_POWER_TEST` 自动扫描。实现 [板级钩子头文件](components/muse/boards/muse_power_test_board.h) 和这块板自己的状态表即可，核心不用改名。共用的电脑端命令行工具是 [`tools/power/power_sweep.py`](tools/power/power_sweep.py)。它的分析器用固件上报的板名，可以用 `--device` 覆盖；Watcher 只作为旧版的备用。

## 改造和扩展

每块板的设置都放在一个很小的 `devices/sdkconfig.<board>` 文件里，加载在 `sdkconfig.defaults` 之上。要加一块板，复制最接近的那份，改芯片、按键引脚、状态灯和闪存大小。[`devices/README.md`](devices/README.md#add-a-board) 写了细节，也可以让 Muse Code 帮你做。

要把自己的头像放到板子屏幕上，插上板子后运行 `python3 tools/muse/avatar.py`。它会让你的 Muse 把头像重画成这块板的像素头像，检查结果，然后编译并烧录。你的头像不会放进 git。原理，以及需要手动步骤的板子，见 [`tools/muse/AVATAR_RECIPE.md`](tools/muse/AVATAR_RECIPE.md)。

要给 Muse 一条它自己的命令，比如读传感器或开关继电器，在 `link.register` 里登记，并在 `main/app.c` 里处理。[`AGENTS.md`](AGENTS.md#adding-a-command) 有逐步说明。反过来，你的程序也能通过 Link 连接读取 Muse 的聊天记录，或读写用户在 VM 上的文件；所有板型都支持。请求路径和限制见 [`AGENTS.md`](AGENTS.md#requests-to-muses-vm-from-your-code)。

要把手机摄像头画面显示在支持的板卡上，用 `MUSE_VIDEO=1 tools/muse/board.sh build <board>` 编译，再在同一 Wi-Fi 下的手机上运行 Muse Gadgets Test（[Android](../android/musegadgetstest)、[iPhone](../ios/musegadgetstest)），并在板卡上打开 Phone camera。手机编码 H.264 或 VP8，板卡解码显示，466 像素圆屏可达 30 fps。协议见 [`protocols/VIDEO.md`](../protocols/VIDEO.md)，支持的板型见 [`AGENTS.md`](AGENTS.md#video-from-a-phone)。

没有板子也能改界面，用 [`simulator/`](simulator/README.md) 的桌面预览。它在一个 412 x 412 的 SenseCAP Watcher 窗口里跑正式版界面和头像渲染，支持鼠标和键盘，也可以在没有显示器服务的情况下按脚本生成截图。

Muse 的回复是文字：按住说话会把你的语音发出去，Muse 把它转成文字并用文字回答；有屏幕的板子会把回答显示成字幕（Voice PE 和 reSpeaker Lite 的回复显示在 Muse 应用里）。有两件事你可以改：

- **更短的回答。** 在消息里直接要求，比如「用一句话回答。」
- **说出来的回答。** 把每条回复的文字发给你选的文字转语音服务，再播放它返回的音频。在有 PSRAM 的板上，接入点是 [`components/muse/muse_chat_session.cpp`](components/muse/muse_chat_session.cpp) 里的 `start_tts`：那里已经有回复文字，MP3 解码、扬声器和音量也都接好了。

要与 Muse 实时通话（实验性），在 `idf.py menuconfig` 的 Muse 菜单打开 **Live voice calls with Muse**（`CONFIG_MUSE_REALTIME_VOICE`）。完整界面且带 PSRAM 的板卡，以及 Voice PE 和 reSpeaker Lite 可启用；默认关闭。使用方式如下：

- **StopWatch：** 按住黄色键说话或打断播放，松开后 Muse 回答并保持通话。录音会保留最多 120 毫秒的尾音。轻按蓝色键挂断。启用实时通话后，黄色键负责说话，蓝色键负责扬声器（通话外静音），与普通固件的两键作用相反；红色键仍负责电源。在 **Settings > Sound > Voice notes** 可切回语音留言和文字回复。打断播放后，已经交给后台的任务仍可稍后报告结果。
- **其他板卡：** 轻按说话键开始通话，再按一次挂断；长按仍发送语音留言。Muse 播放时麦克风关闭，Voice PE 音频芯片报告已启用回声消除时除外。
- Muse 结束通话、安静超过 45 秒（Muse 仍在处理任务的时间除外），或通话达到 15 分钟时也会挂断。可靠的手动结束方式是按键挂断；Muse 说再见后是否自动挂断取决于服务器支持。连接中断后，设备会在重连时请求结束旧通话；若断电或重启，Muse 通常会在约一分钟后结束。
- Voice PE 的静音开关会挂断，开启静音后采集到的音频不会发送。
- 打开 `CONFIG_MUSE_REALTIME_VOICE_GREETING` 可让 Muse 主动问候。
- Muse VM 需要接受来自设备的语音通话。

还有几件值得知道的事：

- 你的 SDK token 会打进固件里，如果是通过串口设置的，则存在 NVS 里，所以把它当成一个身份标识，而不是密码。如果泄露了，到 gadgets.muse.ai 上作废它，生成一个新的，再重新编译，或者通过串口发送新的 token。
- **如果你的板子支持，我们强烈建议打开 NVS 加密。** NVS 是存在闪存里、断电也不会丢的那块存储，里面有 Wi-Fi 密码和设备 token。不加密的话，任何能拿到这块板的人都能读到它们。在 `idf.py menuconfig` 里打开 `CONFIG_HOMEHUB_NVS_ENCRYPTION`（位于 "ESP32 Device SDK" 下面）。加密密钥来自 eFuse 里的一把 HMAC 密钥。eFuse 是芯片里写进去就改不了的存储；如果这块还空着，首次开机时会自动生成密钥。
- 编译出来的版本号是 `999.0.0`。空中升级默认关闭。空中升级就是不插线、通过网络更新固件。跑完整屏幕界面的板子会打开它。用 `idf.py menuconfig` 里的 `CONFIG_HOMEHUB_OTA_ENABLED` 修改，并用 `idf.py -DPROJECT_VER=1.0.0 build` 设置版本号。
- 每次编译都会把生成的设置留在自己的编译目录里（`build/sdkconfig`）。如果你改了 `sdkconfig.defaults` 或某块板的配置文件，删掉编译目录，改动才会生效。
- 编译使用仓库里附带的开发用密钥签名，并且从不打开安全启动，所以你可以随时反复烧录这块板。

## 测试

测试在你的电脑上跑，不需要接板子：

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
```

先跑一次 `idf.py build`，这样下载下来的组件才会就位。电子纸导航测试还需要固定版本的 FreeInk 源码：将 `FREEINK_SOURCE_DIR` 指向对应检出目录，或先配置 Sticky，使用生成的 `managed_components/freeink-sdk`。CI 会检出同一版本。

有两项加密测试，除非这台电脑上具备它们需要的东西，否则会跳过。安装电脑上的 Mbed TLS 库（macOS 上是 `brew install mbedtls pkgconf`，Debian 或 Ubuntu 上是 `apt-get install libmbedtls-dev pkg-config`），并且从一个已经运行过 ESP-IDF `export.sh` 的终端里跑测试。那条命令会设置 `IDF_PATH`。如果有测试被跳过，输出的最后一行会显示 `skipped=`。

## 社区

来我们的社区 [Discord](https://discord.gg/3bhjCkZdd6) 认识其他正在制作、改装 Muse 设备的人。找灵感、互相帮忙，也把你做出来的东西分享出去。

## Gadget 项目共享基础

E1001 和 E1002 已启用设备事件（`events.poll`）、本地时钟（`clock.set`）和原生卡片页面。
[共享基础说明](docs/gadget-foundation.zh-CN.md) 包含接口、控制台入口、资源生成和主机预览方法。

## 许可证

ESP32 设备 SDK 采用 Apache License 2.0，全文见
[`LICENSE`](../LICENSE)。下面这些第三方文件除外，它们继续使用各自上游的许可证：

- [`components/minimp3/include/minimp3.h`](components/minimp3)，MP3 解码器，许可证是 CC0-1.0。见 [`components/minimp3/LICENSE`](components/minimp3/LICENSE)。
- [`main/pixel_font.c`](main/pixel_font.c)，Adafruit GFX 字体，许可证是 BSD-2-Clause，文件头里写了。
- [`components/muse/boards/ostb_echoear_display_init.h`](components/muse/boards/ostb_echoear_display_init.h)，来自 78/xiaozhi-esp32 的 ST77916 面板初始化表，许可证是 MIT，上游声明保留在文件里。
- [`main/card_fonts.c`](main/card_fonts.c) 是 Montserrat Medium 位图，部分标点由现有字形组合而成，采用 SIL OFL 1.1，见[声明和许可全文](main/card_assets_licenses/Montserrat-OFL.txt)。
- [`main/card_icons.c`](main/card_icons.c) 是 Google LLC 的 Material Symbols Rounded 位图，采用 Apache-2.0，见[许可全文](main/card_assets_licenses/MaterialSymbols-LICENSE.txt)。
- [`main/epd_ssd1677.c`](main/epd_ssd1677.c) 是 Sticky Link 状态屏使用的 SSD1677 驱动，基于 Seeed-reTerminal-E10xx-Firmware 的刷新序列，采用 MIT 并保留原版权和许可声明。
- [`board_seeed_xiao_s3_147.c`](components/muse/boards/board_seeed_xiao_s3_147.c) 中改编自 Seeed-Studio/Seeed_GFX2 的 JD9853A 初始化序列采用 MIT，见 [`Seeed_GFX2.txt`](devices/licenses/Seeed_GFX2.txt)。
- [`components/muse/eink/fonts/`](components/muse/eink/fonts/) 中的电子纸字体是 Montserrat（SIL OFL 1.1）、unscii（公有领域）和 Font Awesome 5 Free（SIL OFL 1.1）的子集，详见目录中的 README。
- [`components/tinyh264/src`](components/tinyh264) 是经 udevbe/tinyh264 引入的 Android Open Source Project H.264 解码器，采用 Apache-2.0 并保留 AOSP 版权，见 [`LICENSE`](components/tinyh264/LICENSE)。
- [`components/libvpx_vp8/src`](components/libvpx_vp8) 是精简的 libvpx v1.16.0 VP8 解码器，采用 BSD-3-Clause 并保留 WebM Project 作者版权和 Google 专利授权，见 [`LICENSE`](components/libvpx_vp8/LICENSE) 和 [`PATENTS`](components/libvpx_vp8/PATENTS)。

构建时拉取的 ESP-IDF 组件（位于 `managed_components/`）使用它们自己的许可证。
