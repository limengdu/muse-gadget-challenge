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

# Seeed Studio 设备上的 Muse Gadgets

[English](README.md)

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset=".github/images/muse-gadgets-dark.png">
    <img src=".github/images/muse-gadgets-light.png" width="900" alt="Muse 设备，其中有一块 Seeed reTerminal 电子墨水屏">
  </picture>
</p>

这个仓库可以把你的 Seeed Studio 设备变成一个 **Muse gadget**：一个连上家里 Wi-Fi、能和 Muse 对话的小设备。Muse 是 Muse app 里的 AI 助手。根据设备不同，你可以按住按钮跟 Muse 说话、在屏幕上看它的回复、让 Muse 把图片显示到屏幕上，或者问它房间里的温度和空气情况。

它是开源项目 [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk) 的 Seeed Studio 版本，增加了更多 Seeed 设备的支持。这一页从零开始一步步带你走完，不需要任何编程经验。

> **开始之前：** 安装这个固件会替换设备出厂自带的软件。它由爱好者写给爱好者。设备可能会在重新烧录之前无法使用，也可能让保修失效。标着 *实验性* 的设备还有部分功能没在真机上验证过。

## SenseCraft × Muse Gadgets 挑战赛

先阅读[参赛指南](challenge/README.zh-CN.md)：报名后，在准备硬件和开发项目之前，先准备好用于登录 [CodeCraft Hub](https://codecraft.seeed.cc/hub/) 的**国际站（Global）账号**。指南提供六款参赛设备的配置链接，包括 reTerminal Sticky（实验性支持）和 XIAO 1.47 英寸触摸屏。

参赛项目的源码**必须公开在 GitHub 上**，并在 CodeCraft Hub 发布的项目案例中附上作品源码链接。参赛作品的代码保存在你自己的 GitHub 仓库里。本仓库接收设备支持、通用 Muse 功能和问题修复的 PR。

## 目录

- [参赛指南](challenge/README.zh-CN.md)
- [支持的设备](#支持的设备)
- [需要准备什么](#需要准备什么)
- [第 1 步：获取 SDK token](#第-1-步获取-sdk-token)
- [第 2 步：安装固件](#第-2-步安装固件)
- [第 3 步：在 Muse app 里添加设备](#第-3-步在-muse-app-里添加设备)
- [第 4 步：开始使用](#第-4-步开始使用)
- [常见问题](#常见问题)
- [恢复出厂固件](#恢复出厂固件)
- [给开发者](#给开发者)

## 支持的设备

| 设备 | 接入 Muse 后能做什么 | 安装方式 |
|---|---|---|
| [SenseCAP Watcher](https://www.seeedstudio.com/SenseCAP-Watcher-W1-A-p-5979.html) | 圆形触摸屏上有一张会动的脸。按下滚轮说话，在屏幕上看回复。能显示 Muse 发来的图片。带电池。 | [网页烧录器](#方法-a网页烧录器最简单) |
| [SenseCAP Indicator](https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html) | 4 英寸屏幕上显示状态和 Muse 发来的图片。D1S 和 D1Pro 能把 CO2、tVOC、温度和湿度告诉 Muse。 | [网页烧录器](#方法-a网页烧录器最简单) |
| [reTerminal E1001](https://www.seeedstudio.com/reTerminal-E1001-p-6534.html) | 7.5 英寸黑白电子墨水屏上显示状态，能显示 Muse 发来的黑白图片，带温湿度。 | [网页烧录器](#方法-a网页烧录器最简单) |
| [reTerminal E1002](https://www.seeedstudio.com/reTerminal-E1002-p-6533.html) | 同上，换成 7.3 英寸六色电子墨水屏，能显示彩色图片。 | [网页烧录器](#方法-a网页烧录器最简单) |
| [reTerminal Sticky](https://www.seeedstudio.com/reTerminal-Sticky-p-6861.html) *（实验性）* | 3.97 英寸电子墨水触摸屏上运行 Muse。按住 AI 键发语音，在屏幕上看回复（支持中文和日文）。灰度图片、温湿度、电池。没有喇叭。 | [网页烧录器](#方法-a网页烧录器最简单) |
| [reSpeaker Lite（搭配 XIAO ESP32-S3）](https://wiki.seeedstudio.com/xiao_respeaker/) *（实验性）* | 按住 XIAO 上的 BOOT 键说话，回复显示在 Muse app 里。 | [自己编译](#方法-c自己编译) |
| [XIAO 1.47 英寸 IPS 触摸屏（ESP32-S3）](https://www.seeedstudio.com/1-47-Inch-Touch-Display-Powered-by-XIAO-ESP32-S3-Plus-p-6996.html) | 1.47 英寸触摸屏上有一张会动的脸。按住 USR1 说话，在屏幕上看回复。能显示 Muse 发来的图片。没有喇叭。 | [自己编译](#方法-c自己编译) |
| XIAO AMOLED Display，1.75 英寸圆屏、2.05 英寸屏或 2.16 英寸方屏 *（实验性）* | AMOLED 触摸屏上有一张会动的脸。按住 ENTER 说话，在屏幕上看回复。能显示 Muse 发来的图片，带电池。 | [自己编译](#方法-c自己编译) |

这些设备都只用 2.4 GHz Wi-Fi。包括其他品牌在内的完整板卡列表在 [`esp32/devices/`](esp32/devices/README.md)。

## 需要准备什么

- **你的 Seeed 设备**，充好电或者插着电。
- **一根能传数据的 USB-C 线。** 很多充电器附带的线只能充电，电脑通过它们是认不出设备的。如果第 2 步里什么都没出现，先换根线试试。
- **一台电脑。** 用网页烧录器的话，Windows、macOS、Linux 都行，要用 **Chrome** 或 **Edge** 浏览器。要自己编译的话，需要 macOS 或 Linux。
- **Muse app**，装在你的 iPhone 或安卓手机上，并且已经登录。
- **家里 Wi-Fi 的名字和密码。** 必须是 2.4 GHz 网络；大多数家用路由器在 5 GHz 之外都会同时提供 2.4 GHz。
- **一个 SDK token**，第 1 步会讲。

## 第 1 步：获取 SDK token

每个 Muse gadget 都需要一个 SDK token 才能连上 Muse。它是一串以 `mgst_` 开头的代码，把设备和你的账号绑在一起。

1. 打开 [gadgets.muse.ai](https://gadgets.muse.ai/settings/sdk-tokens)，用你的 Muse 账号登录。
2. 进入 **Account > SDK tokens**，新建一个 token。
3. 复制下来放在手边，第 2 步要粘贴。
4. 阅读 [Gadget SDK 条款](https://gadgets.muse.ai/sdk-terms)。

把这个 token 当成用户名而不是密码：它会存在设备上。如果你觉得别人拿到了它，就在 gadgets.muse.ai 上删掉，新建一个，再给你的设备重新设置一遍。

## 第 2 步：安装固件

按[上面的表格](#支持的设备)里给你的设备写的方式来。

### 方法 A：网页烧录器（最简单）

适用于 **SenseCAP Watcher、SenseCAP Indicator、reTerminal E1001、reTerminal E1002 和 reTerminal Sticky**。全程在浏览器里完成。

> **SenseCAP Watcher 用户请先看这里。** Watcher 里有一小块每台设备都不一样的出厂数据，Muse 会把它覆盖掉。以后要换回 Seeed 的固件就得用到它。请先用[方法 C 的工具备份它](#备份-watcher-的出厂数据)；如果你不打算换回去，可以跳过。

1. 用 USB 线把设备连到电脑。
   - **SenseCAP Watcher：** 用**底部**的 USB-C 口。
2. 用 Chrome 或 Edge 打开 [Muse Gadget Web Flasher](https://seeed-projects.github.io/seeedstudio-muse-gadget-sdk/)。
3. 在 **1 · SDK token** 里粘贴你的 token。页面会记住它，下次不用再填。
4. 在 **2 · Install** 里，从 **Board** 中选你的设备，然后点 **Connect & install**。
5. 浏览器会让你选一个串口。选你设备的那个，点 **连接**。
   - **SenseCAP Watcher：** 会出现两个串口，选**第二个**，也就是编号大的那个。第一个是它的摄像头。
   - **列表里什么都没有？** 换根线试试。有些板子要在插线的时候按住 **BOOT** 键。
6. 点 **Install** 并确认。安装完成之前保持这个标签页在最前面，也别拔线。大约需要一两分钟，Watcher 大约要 4 分钟。
7. 关掉安装窗口。页面会把你的 SDK token 发给设备，设备随后重启。

接着去[第 3 步](#第-3-步在-muse-app-里添加设备)。

以后更新也是同样的步骤，Wi-Fi、配对和 token 都会保留。

### 方法 B：交给 Muse Code

适用于**任何设备**，包括方法 A 不支持的那些。Muse Code 是一个 AI 编程助手，它会读这个仓库里的说明，替你编译和烧录，每条命令执行前都会先问你。它在 macOS 或 Linux 的终端里运行。

1. 打开 **终端（Terminal）** app，安装 Muse Code：

   ```sh
   curl -fsSL https://dev.meta.ai/install.sh | sh
   ```

2. 下载这个仓库，并在里面启动 Muse Code：

   参赛开发时，按[个人 Fork 配置步骤](challenge/README.zh-CN.md#3-fork-并配置-muse-gadget-sdk)，在下面的克隆命令中使用自己 Fork 的地址。

   ```sh
   git clone https://github.com/Seeed-Projects/seeedstudio-muse-gadget-sdk
   cd seeedstudio-muse-gadget-sdk/esp32
   muse --disable-sandbox
   ```

   参赛开发时，先让 Muse Code 阅读 [`challenge/AGENTS.md`](challenge/AGENTS.md)。从 `esp32/` 工作目录访问时，该文件的相对路径为 `../challenge/AGENTS.md`。

   第一次运行时它会让你登录，并确认信任这个文件夹。`--disable-sandbox` 让它能访问 USB 口和下载工具。
3. 插上你的设备，用你自己的话告诉它你有什么。比如：

   > 我有一台 Seeed reTerminal Sticky，帮我把 Muse 固件装上去。我的 SDK token 是 mgst_…

4. 同意它提出的命令。等它说设备准备好了，去[第 3 步](#第-3-步在-muse-app-里添加设备)。

### 方法 C：自己编译

适用于**任何设备**，需要 macOS 或 Linux。你只要把几条命令复制到 **终端（Terminal）** app 里。以 `#` 开头的行是注释，不是命令。

#### 1. 安装工具（只需一次）

固件用乐鑫的 **ESP-IDF 6.0.1 版**编译，其他版本不行。这一步要下载大约 2 GB，需要一段时间。

macOS 上，如果还没有 [Homebrew](https://brew.sh) 就先装上，然后：

```sh
brew install cmake ninja dfu-util python3 git
```

Linux 上，按[乐鑫的 Linux 指南](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/linux-macos-setup.html)安装依赖软件包。然后两种系统都运行：

```sh
git clone -b v6.0.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.0.1
~/esp/esp-idf-v6.0.1/install.sh esp32s3
```

#### 2. 下载这个仓库（只需一次）

参赛开发时，按[个人 Fork 配置步骤](challenge/README.zh-CN.md#3-fork-并配置-muse-gadget-sdk)操作，然后在这个工作目录中继续下面的第 3 步。

```sh
git clone https://github.com/Seeed-Projects/seeedstudio-muse-gadget-sdk
```

#### 3. 为你的设备编译

每次都新开一个终端窗口，先运行这两行：

```sh
. ~/esp/esp-idf-v6.0.1/export.sh
cd seeedstudio-muse-gadget-sdk/esp32
```

然后运行表格里你设备的**编译**命令。第一次编译要好几分钟。

| 设备 | 编译 | 安装（烧录） |
|---|---|---|
| SenseCAP Watcher | `tools/muse/board.sh build watcher` | `tools/muse/board.sh flash watcher` |
| SenseCAP Indicator | `tools/board.sh sensecap-indicator build` | `tools/board.sh sensecap-indicator flash` |
| reTerminal E1001 | `tools/board.sh reterminal-e1001 build` | `tools/board.sh reterminal-e1001 flash` |
| reTerminal E1002 | `tools/board.sh reterminal-e1002 build` | `tools/board.sh reterminal-e1002 flash` |
| reTerminal Sticky | `tools/board.sh reterminal-sticky build` | `tools/board.sh reterminal-sticky flash` |
| reSpeaker Lite（XIAO） | `tools/board.sh seeed-respeaker-lite build` | `tools/board.sh seeed-respeaker-lite flash` |
| XIAO 1.47 英寸触摸屏 | `tools/muse/board.sh build xiao147` | `tools/muse/board.sh flash xiao147` |
| XIAO AMOLED，1.75 英寸圆屏 | `tools/muse/board.sh build xiao-amoled-175` | `tools/muse/board.sh flash xiao-amoled-175` |
| XIAO AMOLED，2.05 英寸屏 | `tools/muse/board.sh build xiao-amoled-205` | `tools/muse/board.sh flash xiao-amoled-205` |
| XIAO AMOLED，2.16 英寸方屏 | `tools/muse/board.sh build xiao-amoled-216` | `tools/muse/board.sh flash xiao-amoled-216` |

#### 4. 填入 SDK token

这一步用于给自己的设备构建固件。通过 CodeCraft Hub 分享固件时，按[发布用固件的构建步骤](challenge/README.zh-CN.md#51-构建用于分享的固件)清空构建配置中的 token、重新编译，只上传 `muse-gadget.bin`，再从 Hub 草稿页烧录验证，并在烧录后填写自己的 token。

第一次编译出来的固件里还没有 SDK token。编译会生成一个以设备命名的设置文件夹：

| 设备 | 设置文件夹 |
|---|---|
| SenseCAP Watcher | `build-muse-sensecap-watcher` |
| SenseCAP Indicator | `build-sensecap-indicator` |
| reTerminal E1001 | `build-reterminal-e1001` |
| reTerminal E1002 | `build-reterminal-e1002` |
| reTerminal Sticky | `build-reterminal-sticky` |
| reSpeaker Lite（XIAO） | `build-seeed-respeaker-lite` |
| XIAO 1.47 英寸触摸屏 | `build-muse-seeed-xiao-s3-147` |
| XIAO AMOLED，1.75 英寸圆屏 | `build-muse-seeed-xiao-amoled-175` |
| XIAO AMOLED，2.05 英寸屏 | `build-muse-seeed-xiao-amoled-205` |
| XIAO AMOLED，2.16 英寸方屏 | `build-muse-seeed-xiao-amoled-216` |

用下面这条命令把 token 写进去。把 `mgst_YOUR_TOKEN` 换成你的 token（引号保留），把文件夹换成你设备的：

```sh
perl -pi -e 's/^CONFIG_GADGET_SDK_TOKEN=.*/CONFIG_GADGET_SDK_TOKEN="mgst_YOUR_TOKEN"/' build-reterminal-sticky/sdkconfig
```

然后再运行一次表格里的**编译**命令。如果提示 token 无效，就从 gadgets.muse.ai 重新复制一遍：它一共 48 个字符，以 `mgst_` 开头。

#### 5. 装到设备上

第一次给 **SenseCAP Watcher** 安装之前，先[备份它的出厂数据](#备份-watcher-的出厂数据)。

插上设备，运行表格里它的**安装（烧录）**命令。它会自己找到设备的 USB 口。等它运行完：Watcher 大约要 3 分钟，而且完成前什么都不显示，千万别中断。

- **reSpeaker Lite：** 线要插在**小的 XIAO 板**上，不是大板。它的 XMOS 音频芯片必须运行 Seeed 的 16 kHz I2S 固件 v1.0.9，见它的[设置说明](esp32/devices/seeed-respeaker-lite.md)。
- **XIAO 1.47 英寸触摸屏和 XIAO AMOLED：** 线插在 XIAO 板的 USB-C 口上。
- **找不到设备，或者找到不止一个：** 在命令最后加上串口，比如 `tools/board.sh reterminal-e1001 flash /dev/cu.usbserial-110`。macOS 上用 `ls /dev/cu.usb*`，Linux 上用 `ls /dev/ttyACM* /dev/ttyUSB*` 列出串口；拔掉设备再运行一次，消失的那个就是它。

以后再安装，Wi-Fi 和配对都会保留。

#### 备份 Watcher 的出厂数据

在 Watcher 第一次装 Muse 之前做一次。插上 Watcher，先运行[为你的设备编译](#3-为你的设备编译)里的那两行，然后：

```sh
# 列出串口：用 Watcher 的第二个串口，也就是编号大的那个
ls /dev/cu.usbmodem*
# 下面的串口只是示例，换成你自己的
tools/muse/paced_esptool.py --chip esp32s3 -p /dev/cu.usbmodem5A7A0185113 read-flash 0x9000 0x32000 nvsfactory.bin
```

把 `nvsfactory.bin` 存在安全的地方：这是唯一的一份。

## 第 3 步：在 Muse app 里添加设备

装好之后，设备会重启并等待设置。屏幕上会显示它的名字，比如 `MuseGadget-A1B2C3` 或 `MuseGadget-Disp-A1B2C3`，或者它的灯会以橙色呼吸闪烁。

1. 手机靠近设备，打开蓝牙。
2. 在 Muse app 里打开 **Settings > Devices**，打开 **Developer mode**。这一步只需要做一次。
3. 在同一页点 **Add Device**（右上角的 **+**）。
4. 选择以 **MuseGadget** 开头、和你设备对得上的那个名字。
5. app 让你在设备上确认时，按设备上的确认键：

   | 设备 | 确认键 |
   |---|---|
   | SenseCAP Watcher | 按下滚轮 |
   | SenseCAP Indicator | 顶部的按键 |
   | reTerminal E1001、E1002 | 绿色按键 |
   | reTerminal Sticky | AI 键 |
   | reSpeaker Lite | XIAO 上的 **BOOT** 键（标着 **B**，在 USB-C 口旁边） |
   | XIAO 1.47 英寸触摸屏 | **USR1** |
   | XIAO AMOLED | **ENTER** |

6. 选择你的 2.4 GHz Wi-Fi，输入密码。
7. 等 app 显示设备已连接。有屏幕的设备会出现 Muse 的脸或状态；reSpeaker Lite 的灯会变绿。

添加设备时必须在设备上按一下，所以附近的人没法背着你把它加走。请在你信任的网络下操作。

## 第 4 步：开始使用

### 和 Muse 说话

在 **SenseCAP Watcher、reTerminal Sticky、reSpeaker Lite、XIAO 1.47 英寸触摸屏和 XIAO AMOLED** 上：

1. **按住**说话键：Watcher 是滚轮，Sticky 是 AI 键，reSpeaker Lite 是 XIAO 的 BOOT 键，XIAO 1.47 英寸触摸屏是 USR1，XIAO AMOLED 是 ENTER。
2. 按住的同时说话。
3. **松开**就发送。

Muse 用文字回答。Watcher、Sticky 和两款 XIAO 屏会把回答显示在屏幕上；reSpeaker Lite 的回答显示在 Muse app 里。

### 让 Muse 使用你的设备

每个设备都会把自己能做什么告诉 Muse，所以你可以直接在 Muse app 里用大白话提要求。比如：

- "在我的 reTerminal 上显示一张日落的图片。" 带屏幕的设备都能显示图片；电子墨水屏要等几秒，六色的 E1002 最多要 30 秒。
- "我房间现在的温度和湿度是多少？" 两款 reTerminal 和 Sticky 都有温湿度传感器；Indicator D1S 和 D1Pro 还能测 CO2 和 tVOC（记得插上包装盒里的 Grove 传感器）。

### 屏幕上的菜单

在 **Watcher、Sticky 和两款 XIAO 屏** 上，在 Muse 的脸上向左滑就能打开 **Settings（设置）**。XIAO AMOLED 上按 **BACK** 也能打开菜单；XIAO 1.47 英寸触摸屏上按 **USR2** 可以息屏和唤醒。在那里可以修改 Wi-Fi、音量、屏幕亮度和息屏时间，查看电池，重置配对，以及关机。

**Watcher** 的滚轮：按下是说话，转动是让屏幕休眠或唤醒。

### 灯和状态的含义

Indicator 和 reTerminal E1001/E1002 会把状态直接写在屏幕上。reSpeaker Lite 用灯来表示：

| 灯 | 含义 |
|---|---|
| 橙色，呼吸闪烁 | 等待设置 |
| 蓝色，呼吸闪烁 | 请按确认键 |
| 蓝色常亮 | 正在连接 Wi-Fi 和 Muse |
| 绿色 | 已连接 |
| 黄色，闪烁 | 正在重新连接 |
| 紫色 | 还没在 Muse app 里设置 |
| 红色，闪烁 | 出错了 |

## 常见问题

**电脑认不出设备。** 换一根 USB 线试试，大多数问题都是只能充电的线导致的。直接插电脑，不要经过 USB 集线器。有些设备需要在插线时按住 **BOOT** 键。

**安装到一半失败了。** 重新插上再试一次。如果完全连不上，让设备进入下载模式：按住 **BOOT**，按一下 **RESET** 再松开，然后松开 **BOOT**，再试。Watcher 只能用网页烧录器或 `tools/muse/board.sh flash watcher`，其他工具在它的 USB 芯片上会失败。

**Muse app 里找不到设备。** 检查 Developer mode 和蓝牙是否都打开了，以及设备是否在等待设置（屏幕上显示它的名字，或者灯是橙色）。如果它之前设置过，先把它重置（见下面）。

**连不上 Wi-Fi。** 用 2.4 GHz 网络，只有 5 GHz 的网络不行。检查密码，并确认设备离路由器不太远。

**换了 SDK token 之后连不上了。** Muse 记住的是设备当初设置时用的那个 token。换掉或删掉 token 之后，要把设备重置，再在 Muse app 里重新添加。

**从头开始（清除 Wi-Fi 和配对）。**

- Watcher、Sticky 和两款 XIAO 屏：**Settings > Muse > Reset pairing**，再点一次确认。
- Indicator、reTerminal E1001/E1002 和 reSpeaker Lite：按住第 3 步里的确认键 **5 秒**。reSpeaker Lite 要先把 Mute 开关打开。

然后在 Muse app 里重新添加。

还是搞不定？到社区 [Discord](https://discord.gg/3bhjCkZdd6) 里问问。

## 恢复出厂固件

Seeed 原版固件和恢复方法在各设备的 [Seeed wiki](https://wiki.seeedstudio.com) 页面上。**SenseCAP Watcher** 要先烧 Seeed 的固件，再把你备份的出厂数据写回去：

```sh
tools/muse/paced_esptool.py --chip esp32s3 -p /dev/cu.usbmodem5A7A0185113 write-flash 0x9000 nvsfactory.bin
```

**reTerminal Sticky** 请在安装 Muse 之前备份好你需要的东西；Seeed 固件的存储布局不一样，换回去会丢掉 Muse 的设置。

## 给开发者

[E1001 家庭关怀看板](projects/care-board/README.zh-CN.md) 提供大字家人留言、本地提醒和按键回报。

[E1002 儿童板](projects/kid-board/README.zh-CN.md) 提供彩色日程时间线、绿色键完成回报和家长授予的星星奖励。
配套 [Muse 技能](projects/kid-board/SKILL.md) 包含每日安排与奖励约定。


XIAO 1.47 英寸触摸屏也已获得上游支持。Sticky 使用 Muse 的 FreeInk 电子纸界面，带触摸设置和键盘；头像每秒刷新一次，图片以四级灰度显示。

| | |
|---|---|
| [**ESP32 Device SDK**](esp32) | 固件本身：编译、烧录、二次开发、添加新板卡、给 Muse 增加新命令。包括其他品牌在内的所有板卡见 [`esp32/devices/`](esp32/devices/README.md)。中文说明见 [`esp32/README.zh-CN.md`](esp32/README.zh-CN.md)。 |
| [**Linux Device SDK**](linux) | 把树莓派或 Linux 电脑变成 Muse gadget。 |
| [**Web flasher**](web-flasher) | 方法 A 用到的浏览器安装器。 |
| [**Muse Gadgets Test**](android/musegadgetstest)（[iPhone](ios/musegadgetstest)） | 通过 Wi-Fi 把手机摄像头或视频片段传到支持的 ESP32 屏幕，由板子解码 H.264 或 VP8；466 像素圆屏可达 30 fps。使用方式见 [`protocols/VIDEO.md`](protocols/VIDEO.md)。 |

AI 编程助手从 [`AGENTS.md`](AGENTS.md) 读起（Claude Code 通过 [`CLAUDE.md`](CLAUDE.md) 读取它）：里面有 Seeed 设备总表、怎么在这些设备上做应用和 Demo，以及怎么贡献代码。每个目录都有一个入门用的 `README.md`，以及给 [Muse Code](https://developer.meta.com/ai/lp/muse-code/) 这类编程助手看的 `AGENTS.md`。电池功耗测量见 [`esp32/docs/power/`](esp32/docs/power/README.md)。

## 社区

到社区 [Discord](https://discord.gg/3bhjCkZdd6) 认识其他做 Muse gadget 的人，找找灵感、互相帮忙、分享你的作品。

## 许可证

Muse Gadgets 采用 Apache License 2.0 许可，见 [`LICENSE`](LICENSE)。以下第三方文件保留各自上游的许可证：

| 路径 | 上游 | 许可证 |
|---|---|---|
| [`board_seeed_xiao_s3_147.c`](esp32/components/muse/boards/board_seeed_xiao_s3_147.c) 里的 JD9853A 初始化序列 | [Seeed-Studio/Seeed_GFX2](https://github.com/Seeed-Studio/Seeed_GFX2) | MIT，见 [`Seeed_GFX2.txt`](esp32/devices/licenses/Seeed_GFX2.txt) |
| [`esp32/components/minimp3/include/minimp3.h`](esp32/components/minimp3) | [lieff/minimp3](https://github.com/lieff/minimp3) | CC0-1.0，见 [`LICENSE`](esp32/components/minimp3/LICENSE) |
| [`esp32/components/muse/boards/ostb_echoear_display_init.h`](esp32/components/muse/boards/ostb_echoear_display_init.h) | [78/xiaozhi-esp32 ESP-VoCat 屏幕初始化序列](https://github.com/78/xiaozhi-esp32/blob/0d576d3d4c049c6f55eaf879725dc23e516511b4/main/boards/espressif/esp-vocat/esp_vocat.cc) | MIT，保留在文件头里 |
| [`esp32/main/pixel_font.c`](esp32/main/pixel_font.c) | Adafruit GFX `glcdfont.c` | BSD-2-Clause，见文件头 |
| [`esp32/main/card_fonts.c`](esp32/main/card_fonts.c) | Montserrat Medium 位图，部分标点由字体原有字形组合而成 | SIL Open Font License 1.1；[声明和许可全文](esp32/main/card_assets_licenses/Montserrat-OFL.txt) |
| [`esp32/main/card_icons.c`](esp32/main/card_icons.c) | Google LLC 的 Material Symbols Rounded | Apache-2.0；[许可全文](esp32/main/card_assets_licenses/MaterialSymbols-LICENSE.txt) |
| [`esp32/main/epd_ssd1677.c`](esp32/main/epd_ssd1677.c) | Seeed-reTerminal-E10xx-Firmware 的 SSD1677 刷新序列 | MIT，保留在文件头里 |
| [`esp32/components/muse/eink/fonts/montserrat.ttf`](esp32/components/muse/eink/fonts) | LVGL v9.5.0 使用的 Montserrat Medium | SIL Open Font License 1.1 |
| [`esp32/components/muse/eink/fonts/unscii.ttf`](esp32/components/muse/eink/fonts) | unscii-8 | 公有领域 |
| [`esp32/components/muse/eink/fonts/symbols.ttf`](esp32/components/muse/eink/fonts) | LVGL v9.5.0 使用的 Font Awesome 5 Free | SIL Open Font License 1.1（图标为 CC BY 4.0） |
| [`esp32/components/tinyh264/src`](esp32/components/tinyh264) | 经 [udevbe/tinyh264](https://github.com/udevbe/tinyh264) 引入的 Android Open Source Project H.264 解码器 | Apache-2.0，保留 AOSP 版权，见 [`LICENSE`](esp32/components/tinyh264/LICENSE) |
| [`esp32/components/libvpx_vp8/src`](esp32/components/libvpx_vp8) | 精简的 [libvpx](https://chromium.googlesource.com/webm/libvpx) v1.16.0 VP8 解码器 | BSD-3-Clause，保留 WebM Project 作者版权，见 [`LICENSE`](esp32/components/libvpx_vp8/LICENSE)，Google 专利授权见 [`PATENTS`](esp32/components/libvpx_vp8/PATENTS) |

编译时下载的依赖遵循各自的许可证：ESP-IDF 组件（下载到 `esp32/managed_components/`），以及模拟器用到的 LVGL 和 SDL（列在 [`esp32/simulator/THIRD_PARTY.md`](esp32/simulator/THIRD_PARTY.md)）。

Apache 许可证不覆盖 [Jollybot 头像](esp32/avatar)。

Muse Gadgets Test 的示例视频由应用在播放时下载 Blender Foundation 开放电影（CC BY 3.0）的几秒片段，并显示署名。

Muse Gadgets Test 可在手机端使用 x264（GPL-2.0）；它的代码由构建时获取。包含它的 APK 或 iPhone 应用适用 GPL，详见 [Android 说明](android/musegadgetstest/README.md)和 [iPhone 说明](ios/musegadgetstest/README.md)。
