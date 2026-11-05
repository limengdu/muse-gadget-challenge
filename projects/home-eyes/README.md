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

# Home Eyes

[中文](README.zh-CN.md)

Away from home, use your own Muse to look at a room, watch a fixed scene for
changes, or leave a message for someone at home. Home Eyes runs on the
Seeed SenseCAP Watcher's onboard camera, 412×412 round touch display, wheel,
speaker and microphone.

## Set up

Use this repository and ESP-IDF **v6.0.1**; see the
[firmware guide](../../esp32/AGENTS.md). Fix the Watcher facing the scene.
USB power is recommended for unattended watches. The existing Himax camera
firmware supplies captures.

Before the **first flash**, save the full flash and unique factory partition
into the ignored backups folder. From esp32/, replace PORT with the Watcher's
console port:

~~~sh
mkdir -p ../backups
python tools/muse/paced_esptool.py --chip esp32s3 -p PORT read-flash 0 ALL ../backups/watcher-factory.bin
python tools/muse/paced_esptool.py --chip esp32s3 -p PORT read-flash 0x9000 0x32000 ../backups/watcher-nvsfactory.bin
tools/muse/board.sh build watcher
idf.py -B build-muse-sensecap-watcher menuconfig
tools/muse/board.sh build watcher
tools/muse/board.sh flash watcher
~~~

The [device guide](../../esp32/devices/README.md#watcher-camera) describes paced
USB transfers and the nvsfactory backup. Keep backups with the specific unit.
In menuconfig, enter your own SDK token under the Device SDK settings
(CONFIG_GADGET_SDK_TOKEN). It stays in the ignored private build configuration.

The Watcher overlay enables CONFIG_MUSE_WATCHER_CAMERA, CONFIG_MUSE_HOME_EYES,
CONFIG_HOMEHUB_GADGET_EVENTS and CONFIG_HOMEHUB_GADGET_CLOCK. Existing builds
can enable these in menuconfig; fresh build directories load the overlay.

Open the Muse phone app, add the nearby Watcher, set its home Wi-Fi and press
the wheel when pairing asks for confirmation. Keep the Watcher on that Wi-Fi;
the phone may be elsewhere. Give Muse the [Home Eyes skill](SKILL.md) contents
or its URL in your repository and ask it to install the skill.

## Things to say

- “Look at the kitchen. Does the stove appear off?”
- “Watch the printer every two minutes until 8 pm. Has the print detached?”
- “Watch the doorway with low sensitivity. Has a parcel arrived?”
- “Tell the kids I'll be home in 20 minutes, from Mom.”
- “What are you watching?” / “Stop watching.”

Use a visible target and a concrete question. Muse inspects the current scene
before starting a watch. Pair a confirmed printer-failure condition with an
explicit request to pause the named printer using the existing
[Moonraker skill](../../skills/gadget-moonraker-3d-printers/SKILL.md).

## Commands

| Command | Parameters and behavior |
|---|---|
| camera.capture | Existing command; returns a JPEG to Muse for a one-time look. |
| watch.start | Required name (1–48 UTF-8 bytes), question (1–192). Optional interval_min (integer 2–120, default 10), sensitivity (low/medium/high, default medium), until (future UTC epoch seconds). Returns state. |
| watch.stop | Ends the watch; cards remain available for acknowledgement. |
| watch.status | Reports active, awaiting_verdict, name, question, interval_min, sensitivity, until, next_check_s, message_unseen, alert_unacknowledged, verdict, last_error and saved. |
| watch.verdict | Required text (1–480 UTF-8 bytes), alert (boolean); accepts a pending change and updates its reference. |
| home.say | Required text (1–480 UTF-8 bytes), from (1–48). Returns acceptance immediately; reading happens later. |
| clock.set | Existing command: utc_epoch and utc_offset_min set UTC and the fixed local offset. |
| events.poll | Existing command; optional since sequence returns recent events. |

SNTP supplies UTC. Set the local offset with clock.set for card timestamps;
the default is UTC. An until deadline requires valid time. A response with
saved=false means background persistence is pending; check watch.status again.
last_error reports a capture or save failure. A later successful capture
clears a capture error.

## At home

The face shows an eye badge, watch name and countdown. A verdict overlays the
face with a soft chime. An alert adds a red ring and repeats a tone every five
seconds until the wheel is pressed. A message card shows sender, text and
local arrival time; an unsynchronized clock is identified on the card.
Long cards scroll vertically by touch.

A wheel press acknowledges the visible card and consumes its release.
Alerts take priority over messages. Dismissing a message reveals any remaining
verdict, then the face. The usual press-to-talk, double-click camera
preview/shutter and navigation resume after dismissal. Muse replies are text;
the device uses tones for attention. One unread-message reminder sounds after
ten minutes. One watch and one unseen message are supported. Finish or stop a
pending watch verdict before replacing its watch; the previous message's
read event must be queued before a new message is accepted.

## Operation and limits

The existing camera path supplies each frame, powering the camera only during
captures. An open live preview supplies its current frame. The ROM decoder
reduces JPEGs at 1/8 scale, then averages to 32×32 grayscale. A 3×3 blur and
global brightness-offset correction reduce noise and exposure changes.
Low/medium/high thresholds are 18/10/5 grayscale levels; hardware tuning for
real scenes remains necessary.

The first frame is the reference. A change posts one event asking Muse to call
camera.capture and watch.verdict. Checks wait for that verdict. Afterwards
the changed frame becomes the reference and a new interval begins.
Unacknowledged alerts pause checks. Camera failures retry at the next interval
and appear in watch.status. The worker waits for deadlines or state changes.

State copies, comparison thumbnails, card data and event-sender memory use the
Watcher's PSRAM. The worker retains a 4 KiB internal stack for NVS writes and
logs its minimum free stack once after the first successful capture and save.
Camera reads use 512-byte DMA chunks; capture and decoder buffers are released
after each check. The connected-device target is at least 22 KiB free DMA RAM,
including two-minute checks, above Link's 16 KiB send margin. The measured
static/stack saving predicts about 24.5 KiB between checks; sustained device
verification is pending in the [validation record](VERIFICATION.md#ram-margin-follow-up-2026-11-05).

Configuration, cards and pending acknowledgements persist in NVS. Pictures and
thumbnails stay in RAM. Reboot establishes a new reference and reissues a pending
verdict request. Valid UTC restores saved deadlines and message age; without
valid UTC a fresh baseline resumes the watch, while an until deadline waits
for synchronization. Foundation events may be delayed, repeated around reboot
or dropped under offline queue pressure; inspect watch.status and events.poll.

The screen, codecs and CPU retain the Watcher's battery sleep path. Wi-Fi
stays associated in modem sleep for remote access, replacing the prolonged
Wi-Fi-off nap. Battery use and capture/sleep timing require device tests;
use USB power for long unattended watches.

Photos requested by Muse go only through the user's own paired Muse.
Home Eyes stores photos on neither flash nor SD. Comparison thumbnails exist
only in RAM; settings and message/verdict text are saved on the device.
Account-side photo retention follows the user's Muse settings.
Visual monitoring is an attention aid; a photo alone cannot establish safety.

## Verification

From esp32/:

~~~sh
python3 -m unittest tests.test_home_eyes tests.test_gadget_events
python3 -m unittest discover -s tests -p 'test_*.py'
tools/muse/board.sh build watcher
~~~

Host tests cover diff thresholds, exposure/noise, scheduling, expiry,
verdict/alert/acknowledgement, messages/reminders, validation, persistence,
event retries and full registration size with the feature on/off.
Memory checks cover PSRAM event allocation, capture-buffer release on failure
and repeated captures, bounded camera reads, and the one-time stack log.
The [simulator](../../esp32/simulator/README.md) includes round 412×412 watching,
message, verdict, alert and dismiss-to-face scenarios.

Device verification covers fresh build size, paired remote captures/events,
live preview with timed checks, long-text scrolling, wheel acknowledgements,
chimes and alarm stop, reboot recovery, sleeping Wi-Fi reachability and battery
rest between captures. Build, simulator and hardware results are separate.
Pairing, remote camera.capture, a home.say acknowledgement event and watch.start
worked on one Watcher before the RAM fix. A later DMA shortage stalled replies
and tunnel keepalives; the fix's DMA minimum and complete watch/verdict cycle
remain device checks.

Detailed build, host-test and remaining device checks: [validation record](VERIFICATION.md).
