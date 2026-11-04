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

# Muse Gadgets for Seeed Studio devices

[中文](README.zh-CN.md)

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset=".github/images/muse-gadgets-dark.png">
    <img src=".github/images/muse-gadgets-light.png" width="900" alt="Muse gadgets, including a Seeed reTerminal e-ink display">
  </picture>
</p>

This repository turns a Seeed Studio device into a **Muse gadget**: a small
companion that joins your Wi-Fi and talks to Muse, the AI assistant in the
Muse app. Depending on the device, you can hold a button and talk to Muse, read
its replies on the screen, have Muse show pictures on the display, or ask it
about the temperature and air in the room.

It's Seeed Studio's copy of the open source
[Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk), with
support for more Seeed devices. This page walks you through it from the
beginning, with no programming experience needed.

> **Before you start:** installing this firmware replaces the software your
> device came with. It's built by hobbyists, for hobbyists. It can stop a
> device from working until it's flashed again, and it may void your warranty.
> On devices marked *experimental*, some features haven't been checked on the
> hardware yet.

## SenseCraft × Muse Gadgets Challenge

Start with the [challenge guide](challenge/README.md): sign up and prepare a
**Global (International) account** for [CodeCraft Hub](https://codecraft.seeed.cc/hub/)
before preparing hardware or developing your project. The guide includes
configuration links for all six challenge devices, including reTerminal
Sticky (experimental) and the XIAO 1.47 Touch Display.

Publish your project's source code in a **public GitHub repository** and
include a project source link in the case you submit on CodeCraft Hub.
Keep your project's code in your own GitHub repository. This repository
accepts pull requests for device support, board-neutral Muse features and fixes.

## Contents

- [Challenge guide](challenge/README.md)
- [Supported devices](#supported-devices)
- [What you need](#what-you-need)
- [Step 1: Get your SDK token](#step-1-get-your-sdk-token)
- [Step 2: Install the firmware](#step-2-install-the-firmware)
- [Step 3: Add the device in the Muse app](#step-3-add-the-device-in-the-muse-app)
- [Step 4: Use it](#step-4-use-it)
- [Troubleshooting](#troubleshooting)
- [Going back to the factory firmware](#going-back-to-the-factory-firmware)
- [For developers](#for-developers)

## Supported devices

| Device | What it does with Muse | How to install |
|---|---|---|
| [SenseCAP Watcher](https://www.seeedstudio.com/SenseCAP-Watcher-W1-A-p-5979.html) | Animated face on its round touch screen. Press the wheel to talk, read the reply on screen. Shows pictures from Muse. Battery. | [Web flasher](#option-a-the-web-flasher-easiest) |
| [SenseCAP Indicator](https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html) | Status on its 4" screen, pictures from Muse. The D1S and D1Pro report CO2, tVOC, temperature and humidity to Muse. | [Web flasher](#option-a-the-web-flasher-easiest) |
| [reTerminal E1001](https://www.seeedstudio.com/reTerminal-E1001-p-6534.html) | Status on a 7.5" black-and-white e-paper, black-and-white pictures from Muse, temperature and humidity. | [Web flasher](#option-a-the-web-flasher-easiest) |
| [reTerminal E1002](https://www.seeedstudio.com/reTerminal-E1002-p-6533.html) | The same on a 7.3" six-colour e-paper, with colour pictures. | [Web flasher](#option-a-the-web-flasher-easiest) |
| [reTerminal Sticky](https://www.seeedstudio.com/reTerminal-Sticky-p-6861.html) *(experimental)* | Muse on a 3.97" e-paper touch screen. Hold the AI button to send a voice note, read the reply on screen (Chinese and Japanese too). Grayscale pictures, temperature, humidity, battery. No speaker. | [Web flasher](#option-a-the-web-flasher-easiest) |
| [reSpeaker Lite with XIAO ESP32-S3](https://wiki.seeedstudio.com/xiao_respeaker/) *(experimental)* | Hold the XIAO's BOOT button to talk. Replies appear in the Muse app. | [Build it](#option-c-build-it-yourself) |
| [XIAO 1.47" IPS Touch Display (ESP32-S3)](https://www.seeedstudio.com/1-47-Inch-Touch-Display-Powered-by-XIAO-ESP32-S3-Plus-p-6996.html) | Animated face on a 1.47" touch screen. Hold USR1 to talk, read the reply on screen. Pictures from Muse. No speaker. | [Build it](#option-c-build-it-yourself) |
| XIAO AMOLED Display, 1.75" round, 2.05" or 2.16" square screen *(experimental)* | Animated face on an AMOLED touch screen. Hold ENTER to talk, read the reply on screen. Pictures from Muse, battery. | [Build it](#option-c-build-it-yourself) |

All of them use 2.4 GHz Wi-Fi. The full list of boards, including other
brands, is in [`esp32/devices/`](esp32/devices/README.md).

## What you need

- **Your Seeed device**, charged or plugged in.
- **A USB-C cable that carries data.** Many cables that came with chargers
  only carry power, and the computer won't see the device through them. If
  nothing shows up in Step 2, try another cable first.
- **A computer.** For the web flasher: Windows, macOS or Linux with the
  **Chrome** or **Edge** browser. To build it yourself: macOS or Linux.
- **The Muse app** on your iPhone or Android phone, signed in to your account.
- **Your home Wi-Fi name and password.** The network has to be 2.4 GHz; most
  home routers offer one alongside 5 GHz.
- **An SDK token**, which Step 1 explains.

## Step 1: Get your SDK token

Every Muse gadget needs an SDK token to connect to Muse. It's a code that
starts with `mgst_`, and it ties the gadget to your account.

1. Open [gadgets.muse.ai](https://gadgets.muse.ai/settings/sdk-tokens) and
   sign in with your Muse account.
2. Go to **Account > SDK tokens** and create a token.
3. Copy it and keep it somewhere handy. You'll paste it in Step 2.
4. Read the [Gadget SDK Terms](https://gadgets.muse.ai/sdk-terms).

Treat the token like a username rather than a password: it's stored on the
device. If you think someone else has it, delete it on gadgets.muse.ai, make a
new one, and set it up on your devices again.

## Step 2: Install the firmware

Pick the option the [table above](#supported-devices) gives for your device.

### Option A: the web flasher (easiest)

For the **SenseCAP Watcher, SenseCAP Indicator, reTerminal E1001,
reTerminal E1002 and reTerminal Sticky**. Everything happens in your browser.

> **SenseCAP Watcher owners, read this first.** The Watcher keeps a small block
> of factory data that's unique to each device, and Muse writes over it. You
> need it to go back to Seeed's firmware later. Back it up first with
> [Option C's tools](#back-up-the-watchers-factory-data); if you don't plan to
> go back, you can skip it.

1. Plug the device into your computer with the USB cable.
   - **SenseCAP Watcher:** use the USB-C port on the **bottom**.
2. Open the
   [Muse Gadget Web Flasher](https://seeed-projects.github.io/seeedstudio-muse-gadget-sdk/)
   in Chrome or Edge.
3. In **1 · SDK token**, paste your token. The page remembers it for next time.
4. In **2 · Install**, choose your device under **Board**, then click
   **Connect & install**.
5. The browser asks you to pick a serial port. Choose the one for your device
   and click **Connect**.
   - **SenseCAP Watcher:** two ports appear. Pick the **second** one, with
     the higher number. The first is its camera.
   - **Nothing in the list?** Try another cable. Some boards need you to hold
     the **BOOT** button while you plug in the cable.
6. Click **Install**, and confirm. Keep the tab in front and the cable plugged
   in until it says it's done. It takes a minute or two; the Watcher takes
   about 4 minutes.
7. Close the installer window. The page then sends your SDK token to the
   device, which restarts.

Go to [Step 3](#step-3-add-the-device-in-the-muse-app).

Updating later works the same way and keeps your Wi-Fi, pairing and token.

### Option B: let Muse Code do it

For **any device**, including the ones Option A doesn't have. Muse Code is an
AI coding assistant that reads this repository's instructions and does the
building and flashing for you, asking before each command it runs. It runs in
the terminal on macOS or Linux.

1. Open the **Terminal** app and install Muse Code:

   ```sh
   curl -fsSL https://dev.meta.ai/install.sh | sh
   ```

2. Download this repository and start Muse Code in it:

   For challenge work, use your own Fork's URL in the clone command below,
   following the [participant checkout setup](challenge/README.md#3-fork-and-set-up-the-muse-gadget-sdk).

   ```sh
   git clone https://github.com/Seeed-Projects/seeedstudio-muse-gadget-sdk
   cd seeedstudio-muse-gadget-sdk/esp32
   muse --disable-sandbox
   ```

   For challenge work, first ask Muse Code to read
   [`challenge/AGENTS.md`](challenge/AGENTS.md). From the `esp32/` working
   directory, that file is `../challenge/AGENTS.md`.

   The first time, it asks you to sign in and to trust the folder.
   `--disable-sandbox` lets it reach the USB port and download the tools.
3. Plug in your device and tell it what you have, in your own words. For
   example:

   > I have a Seeed reTerminal Sticky. Install the Muse firmware on it. My SDK
   > token is mgst_…

4. Approve the commands it proposes. When it says the device is ready, go to
   [Step 3](#step-3-add-the-device-in-the-muse-app).

### Option C: build it yourself

For **any device**, on macOS or Linux. You'll copy a few commands into the
**Terminal** app. Lines starting with `#` are notes, not commands.

#### 1. Install the tools (once)

The firmware is built with Espressif's **ESP-IDF version 6.0.1**; other
versions won't work. This downloads about 2 GB and takes a while.

On macOS, install [Homebrew](https://brew.sh) if you don't have it, then:

```sh
brew install cmake ninja dfu-util python3 git
```

On Linux, install the packages in
[Espressif's Linux guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/linux-macos-setup.html).
Then, on either:

```sh
git clone -b v6.0.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.0.1
~/esp/esp-idf-v6.0.1/install.sh esp32s3
```

#### 2. Download this repository (once)

For challenge work, use the [participant Fork setup](challenge/README.md#3-fork-and-set-up-the-muse-gadget-sdk)
and continue from that checkout in step 3 below.

```sh
git clone https://github.com/Seeed-Projects/seeedstudio-muse-gadget-sdk
```

#### 3. Build for your device

Open a new Terminal window each time, and start with these two lines:

```sh
. ~/esp/esp-idf-v6.0.1/export.sh
cd seeedstudio-muse-gadget-sdk/esp32
```

Then build with your device's command from this table. The first build takes
several minutes.

| Device | Build | Install (flash) |
|---|---|---|
| SenseCAP Watcher | `tools/muse/board.sh build watcher` | `tools/muse/board.sh flash watcher` |
| SenseCAP Indicator | `tools/board.sh sensecap-indicator build` | `tools/board.sh sensecap-indicator flash` |
| reTerminal E1001 | `tools/board.sh reterminal-e1001 build` | `tools/board.sh reterminal-e1001 flash` |
| reTerminal E1002 | `tools/board.sh reterminal-e1002 build` | `tools/board.sh reterminal-e1002 flash` |
| reTerminal Sticky | `tools/board.sh reterminal-sticky build` | `tools/board.sh reterminal-sticky flash` |
| reSpeaker Lite (XIAO) | `tools/board.sh seeed-respeaker-lite build` | `tools/board.sh seeed-respeaker-lite flash` |
| XIAO 1.47" Touch Display | `tools/muse/board.sh build xiao147` | `tools/muse/board.sh flash xiao147` |
| XIAO AMOLED, 1.75" round | `tools/muse/board.sh build xiao-amoled-175` | `tools/muse/board.sh flash xiao-amoled-175` |
| XIAO AMOLED, 2.05" | `tools/muse/board.sh build xiao-amoled-205` | `tools/muse/board.sh flash xiao-amoled-205` |
| XIAO AMOLED, 2.16" square | `tools/muse/board.sh build xiao-amoled-216` | `tools/muse/board.sh flash xiao-amoled-216` |

#### 4. Add your SDK token

This step prepares a build for your own device. For firmware shared through
CodeCraft Hub, follow the [release build steps](challenge/README.md#51-build-the-firmware-for-sharing):
clear the build's token, rebuild, upload only `muse-gadget.bin`, and test
that uploaded image from the Hub draft with your token supplied after flashing.

The first build has no SDK token in it yet. It makes a settings folder named
after your device:

| Device | Settings folder |
|---|---|
| SenseCAP Watcher | `build-muse-sensecap-watcher` |
| SenseCAP Indicator | `build-sensecap-indicator` |
| reTerminal E1001 | `build-reterminal-e1001` |
| reTerminal E1002 | `build-reterminal-e1002` |
| reTerminal Sticky | `build-reterminal-sticky` |
| reSpeaker Lite (XIAO) | `build-seeed-respeaker-lite` |
| XIAO 1.47" Touch Display | `build-muse-seeed-xiao-s3-147` |
| XIAO AMOLED, 1.75" round | `build-muse-seeed-xiao-amoled-175` |
| XIAO AMOLED, 2.05" | `build-muse-seeed-xiao-amoled-205` |
| XIAO AMOLED, 2.16" square | `build-muse-seeed-xiao-amoled-216` |

Put your token in it with this command. Replace `mgst_YOUR_TOKEN` with your
token, keeping the quotes, and the folder with your device's:

```sh
perl -pi -e 's/^CONFIG_GADGET_SDK_TOKEN=.*/CONFIG_GADGET_SDK_TOKEN="mgst_YOUR_TOKEN"/' build-reterminal-sticky/sdkconfig
```

Then run the **Build** command from the table again. If it says the token
isn't valid, copy it again from gadgets.muse.ai: it's 48
characters long and starts with `mgst_`.

#### 5. Install it on the device

Before installing on a **SenseCAP Watcher** for the first time,
[back up its factory data](#back-up-the-watchers-factory-data).

Plug in the device and run its **Install (flash)** command from the table. It
finds the device's USB port by itself. Wait for it to finish; the Watcher
takes about 3 minutes and prints nothing until it's done, so don't interrupt
it.

- **reSpeaker Lite:** plug the cable into the **small XIAO board**, not the
  larger board. Its XMOS audio chip must run Seeed's 16 kHz I2S firmware
  v1.0.9; see its [setup guide](esp32/devices/seeed-respeaker-lite.md).
- **XIAO 1.47" Touch Display and XIAO AMOLED:** plug the cable into the XIAO
  board's USB-C port.
- **It can't find the device, or finds more than one:** add the port at the
  end, such as `tools/board.sh reterminal-e1001 flash /dev/cu.usbserial-110`.
  `ls /dev/cu.usb*` on macOS, or `ls /dev/ttyACM* /dev/ttyUSB*` on Linux,
  lists them; unplug the device and run it again to see which one disappears.

Installing again later keeps your Wi-Fi and pairing.

#### Back up the Watcher's factory data

Do this once, before Muse goes on a Watcher for the first time. With the
Watcher plugged in, and the two lines from
[Build for your device](#3-build-for-your-device) run:

```sh
# list the ports: use the second Watcher port, the one with the higher number
ls /dev/cu.usbmodem*
# the port below is an example: replace it with yours
tools/muse/paced_esptool.py --chip esp32s3 -p /dev/cu.usbmodem5A7A0185113 read-flash 0x9000 0x32000 nvsfactory.bin
```

Replace the port with yours. Keep `nvsfactory.bin` somewhere safe: it's the
only copy.

## Step 3: Add the device in the Muse app

After installing, the device restarts and waits to be set up. Its screen shows
its name, such as `MuseGadget-A1B2C3` or `MuseGadget-Disp-A1B2C3`, or its
light breathes orange.

1. Keep your phone near the device, with Bluetooth on.
2. In the Muse app, open **Settings > Devices** and turn on **Developer
   mode**. You only do this once.
3. On the same page, tap **Add Device** (the **+** at the top right).
4. Pick the name that starts with **MuseGadget** and matches your device.
5. When the app asks you to confirm on the device, press its confirm button:

   | Device | Confirm button |
   |---|---|
   | SenseCAP Watcher | Press the wheel |
   | SenseCAP Indicator | The button on top |
   | reTerminal E1001, E1002 | The green button |
   | reTerminal Sticky | The AI button |
   | reSpeaker Lite | The XIAO's **BOOT** button (marked **B**, beside the USB-C port) |
   | XIAO 1.47" Touch Display | **USR1** |
   | XIAO AMOLED | **ENTER** |

6. Choose your 2.4 GHz Wi-Fi and type its password.
7. Wait until the app shows the device as connected. On a screen, Muse's face
   or status appears; on the reSpeaker Lite, the light turns green.

Setting a device up only works with that press on the device, so nobody nearby
can add it without you. Do it on a network you trust.

## Step 4: Use it

### Talking to Muse

On the **SenseCAP Watcher, reTerminal Sticky, reSpeaker Lite, XIAO 1.47"
Touch Display and XIAO AMOLED**:

1. **Hold** the talk button: the Watcher's wheel, the Sticky's AI button, the
   XIAO's BOOT button on the reSpeaker Lite, USR1 on the XIAO 1.47" Touch
   Display, or ENTER on the XIAO AMOLED.
2. Speak while you hold it.
3. **Let go** to send.

Muse answers in text. The Watcher, Sticky and both XIAO displays show it on
screen;
the reSpeaker Lite's answers appear in the Muse app.

### Asking Muse to use the device

Every gadget registers what it can do with Muse, so in the Muse app you can ask
for it in plain words. For example:

- "Show a picture of a sunset on my reTerminal." Devices with a screen can
  show pictures; e-paper takes a few seconds, and up to 30 on the six-colour
  E1002.
- "What's the temperature and humidity in my room?" The reTerminals and the
  Sticky have a temperature and humidity sensor; the Indicator D1S and D1Pro
  also measure CO2 and tVOC (plug in the Grove sensor that came in the box).

### The on-screen menu

On the **Watcher, Sticky and both XIAO displays**, swipe left on Muse's face to
open **Settings**. On the XIAO AMOLED, **BACK** opens the menu too; on the
XIAO 1.47" Touch Display, **USR2** turns the screen off and on. From there you
can change Wi-Fi, the volume, the screen's brightness and sleep time, check
the battery, reset the pairing, and power off.

The **Watcher**'s wheel: press to talk, turn to put the screen to sleep or wake
it.

### Light and status meanings

On the Indicator and reTerminal E1001/E1002 the status is written on the
screen. On the reSpeaker Lite, the light shows it:

| Light | Meaning |
|---|---|
| Orange, breathing | Waiting to be set up |
| Blue, breathing | Press the confirm button |
| Blue | Joining Wi-Fi and connecting to Muse |
| Green | Connected |
| Yellow, blinking | Reconnecting |
| Purple | Not set up in the Muse app |
| Red, blinking | Something went wrong |

## Troubleshooting

**The computer doesn't see the device.** Try another USB cable; most failures
are charge-only cables. Plug straight into the computer rather than a hub. Some
devices need you to hold **BOOT** while you plug in the cable.

**Installing fails halfway.** Plug in again and retry. If it can't connect at
all, put the device in download mode: hold **BOOT**, press and release
**RESET**, then let go of **BOOT**, and try again. On the Watcher, use only the
web flasher or `tools/muse/board.sh flash watcher`; other tools fail on its USB
chip.

**The device doesn't appear in the Muse app.** Check that Developer mode is on
and Bluetooth is on, and that the device is waiting to be set up (its name on
screen, or an orange light). If it was set up before, reset it first (below).

**It won't join Wi-Fi.** Use a 2.4 GHz network; 5 GHz-only networks don't
work. Check the password, and that the device isn't too far from the router.

**It stopped connecting after I changed my SDK token.** Muse keeps the token a
device had when you set it up. After changing or deleting a token, reset the
device and add it in the Muse app again.

**Start over (forget Wi-Fi and pairing).**

- Watcher, Sticky and both XIAO displays: **Settings > Muse > Reset pairing**, and tap
  it again to confirm.
- Indicator, reTerminal E1001/E1002 and reSpeaker Lite: hold the confirm button
  from Step 3 for **5 seconds**. On the reSpeaker Lite, turn the Mute switch on
  first.

Then add it in the Muse app again.

Still stuck? Ask in the community [Discord](https://discord.gg/3bhjCkZdd6).

## Going back to the factory firmware

Seeed's own firmware and instructions for putting it back are on each device's
[Seeed wiki](https://wiki.seeedstudio.com) page. On the **SenseCAP Watcher**,
flash Seeed's firmware first, then write back the factory data you saved:

```sh
tools/muse/paced_esptool.py --chip esp32s3 -p /dev/cu.usbmodem5A7A0185113 write-flash 0x9000 nvsfactory.bin
```

On the **reTerminal Sticky**, back up anything you need before installing
Muse; Seeed's firmware uses a different layout, so switching back loses Muse's
settings.

## For developers

[E1001 Care Board](projects/care-board/README.md) adds large-print family messages,
local reminders and key-press events for remote family care.


The XIAO 1.47 Touch Display is also supported upstream. Sticky uses Muse's
FreeInk e-paper UI, with touch settings and a keyboard. Its avatar refreshes
once a second; images use four grays.

| | |
|---|---|
| [**ESP32 Device SDK**](esp32) | The firmware: build, flash and extend it, add a board, give Muse new commands. Every board, including other brands, is in [`esp32/devices/`](esp32/devices/README.md). |
| [**Linux Device SDK**](linux) | Turn a Raspberry Pi or Linux box into a Muse gadget. |
| [**Web flasher**](web-flasher) | The browser installer used in Option A. |
| [**Muse Gadgets Test**](android/musegadgetstest) ([iPhone](ios/musegadgetstest)) | Stream your phone's camera, or a clip, to an ESP32 gadget's screen over Wi-Fi: H.264 or VP8 decoded on the board, 30 fps on the round 466 px boards. [`protocols/VIDEO.md`](protocols/VIDEO.md) explains how. |

Coding agents start at [`AGENTS.md`](AGENTS.md), which Claude Code reads
through [`CLAUDE.md`](CLAUDE.md): the Seeed devices, how to build apps and
demos on them, and how to contribute. Each directory also has a `README.md` to
start with and an `AGENTS.md` for coding agents like
[Muse Code](https://developer.meta.com/ai/lp/muse-code/). For battery power
profiling, see [`esp32/docs/power/`](esp32/docs/power/README.md).

## Community

Meet other people building Muse gadgets in the community
[Discord](https://discord.gg/3bhjCkZdd6). Get inspired, help each other, and
share what you make.

## License

Muse Gadgets is licensed under the Apache License, Version 2.0, found in
[`LICENSE`](LICENSE), except for these third-party files, which keep their
upstream licenses:

| Path | Upstream | License |
|---|---|---|
| JD9853A initialization sequence in [`board_seeed_xiao_s3_147.c`](esp32/components/muse/boards/board_seeed_xiao_s3_147.c) | [Seeed-Studio/Seeed_GFX2](https://github.com/Seeed-Studio/Seeed_GFX2) | MIT, see [`Seeed_GFX2.txt`](esp32/devices/licenses/Seeed_GFX2.txt) |
| [`esp32/components/minimp3/include/minimp3.h`](esp32/components/minimp3) | [lieff/minimp3](https://github.com/lieff/minimp3) | CC0-1.0, see [`LICENSE`](esp32/components/minimp3/LICENSE) |
| [`esp32/components/muse/boards/ostb_echoear_display_init.h`](esp32/components/muse/boards/ostb_echoear_display_init.h) | [78/xiaozhi-esp32 ESP-VoCat panel sequence](https://github.com/78/xiaozhi-esp32/blob/0d576d3d4c049c6f55eaf879725dc23e516511b4/main/boards/espressif/esp-vocat/esp_vocat.cc) | MIT, retained in the file header |
| [`esp32/main/pixel_font.c`](esp32/main/pixel_font.c) | Adafruit GFX `glcdfont.c` | BSD-2-Clause, in the file header |
| [`esp32/components/muse/eink/fonts/montserrat.ttf`](esp32/components/muse/eink/fonts) | Montserrat Medium, via LVGL v9.5.0 | SIL Open Font License 1.1 |
| [`esp32/components/muse/eink/fonts/unscii.ttf`](esp32/components/muse/eink/fonts) | unscii-8 | Public domain |
| [`esp32/components/muse/eink/fonts/symbols.ttf`](esp32/components/muse/eink/fonts) | Font Awesome 5 Free, via LVGL v9.5.0 | SIL Open Font License 1.1 (icons CC BY 4.0) |
| [`esp32/main/card_fonts.c`](esp32/main/card_fonts.c) | Montserrat Medium bitmap masks, with punctuation assembled from the font's shapes | SIL Open Font License 1.1; [notice and license](esp32/main/card_assets_licenses/Montserrat-OFL.txt) |
| [`esp32/main/card_icons.c`](esp32/main/card_icons.c) | Material Symbols Rounded, Google LLC | Apache-2.0; [license](esp32/main/card_assets_licenses/MaterialSymbols-LICENSE.txt) |
| [`esp32/main/epd_ssd1677.c`](esp32/main/epd_ssd1677.c) | Seeed-reTerminal-E10xx-Firmware's SSD1677 refresh sequence | MIT, retained in the file header |
| [`esp32/components/tinyh264/src`](esp32/components/tinyh264) | the Android Open Source Project's H.264 decoder, via [udevbe/tinyh264](https://github.com/udevbe/tinyh264) | Apache-2.0, AOSP's copyright, see [`LICENSE`](esp32/components/tinyh264/LICENSE) |
| [`esp32/components/libvpx_vp8/src`](esp32/components/libvpx_vp8) | [libvpx](https://chromium.googlesource.com/webm/libvpx) v1.16.0's VP8 decoder, trimmed | BSD-3-Clause, the WebM Project authors' copyright, see [`LICENSE`](esp32/components/libvpx_vp8/LICENSE), with Google's patent grant in [`PATENTS`](esp32/components/libvpx_vp8/PATENTS) |

Dependencies fetched at build time are under their own licenses: ESP-IDF
components (into `esp32/managed_components/`), and the simulator's LVGL and
SDL (listed in [`esp32/simulator/THIRD_PARTY.md`](esp32/simulator/THIRD_PARTY.md)).

The Apache License does not cover the [Jollybot avatar](esp32/avatar).

Muse Gadgets Test's sample clips aren't in this repository: the app fetches a few
seconds of the Blender Foundation's open movies (CC BY 3.0) when one is
played, and shows their credits.

Muse Gadgets Test can use x264 (GPL-2.0) on the phone; this repository carries none
of its code. An APK or iPhone app you build with it is under the GPL
([`android/musegadgetstest/README.md`](android/musegadgetstest/README.md),
[`ios/musegadgetstest/README.md`](ios/musegadgetstest/README.md)).
