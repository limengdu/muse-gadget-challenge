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

# Fridge Notes

[简体中文](README.zh-CN.md)

A family list on the fridge, for the person who notices an empty egg carton with
both hands full and the person shopping later. Hold the reTerminal Sticky's AI
button, say what is needed, and let Muse add it. Anyone at home can tick a row.
Muse can read the last mirrored list from your VM while the Sticky sleeps.

## What it does

- Four named lists, for example Shopping, Chores, To-do and Notes, with twenty
  items each. Items have a stable numeric ID, text, a checkbox, an optional
  responsible person and a UTC creation time.
- Lists survive a reboot in the device's NVS storage. A failed save leaves the
  previous list in place and shows a retry message.
- The FreeInk home page has list tabs, large checkbox rows, grayed and struck
  completed items, a Clear done button, battery percentage and sync status.
- Changes wait about three seconds before mirroring all lists to
  `gadget/fridge-notes/lists.json`, relative to your home on your Muse VM.
  Offline uploads retry after thirty seconds. Touch edits also send short
  `[fridge-notes event]` messages to your own Muse chat; bursts become one message.
- Voice notes use the existing Muse voice pipeline. Replies are text on screen;
  after a reply finishes, the list returns.

## Hardware and status

Use the **Seeed reTerminal Sticky**, the experimental full-UI Muse profile:
480×800 portrait SSD1677 e-paper, GT911 touch, four gray levels, a PDM microphone
and battery gauge. The AI button records a voice note and confirms pairing;
Up/Down wake the device. Replies use text because this board has no speaker.
The existing board driver supplies all hardware pins; this project adds no
peripherals. See the [Sticky hardware and validation notes](../../esp32/devices/README.md)
and [firmware instructions](../../esp32/AGENTS.md).

This application is experimental. Host tests exercise the list model, persistence,
VM protocol and FreeInk navigation. This application's panel response, live VM
uploads, voice-to-command path, sleep/wake and battery behaviour await hardware
validation. See [verification results](VALIDATION.md).

## Build, provision and install the skill

1. Install **ESP-IDF v6.0.1**, open this repository and activate its `export.sh`.
   From `esp32/`, build the full-UI profile:

   ```sh
   tools/board.sh reterminal-sticky build
   ```

   `devices/sdkconfig.muse` and `devices/sdkconfig.reterminal-sticky` enable
   `MUSE_STICKY_NOTES`, which selects gadget events and the clock. Existing
   generated configs keep their saved settings: enable Fridge Notes under
   ESP32 Device SDK in `idf.py -B build-reterminal-sticky menuconfig`, then rebuild.
   Its option is available only with the Sticky board and daemon requests enabled.

2. Obtain your Muse SDK token. In `idf.py -B build-reterminal-sticky menuconfig`,
   set the gadget SDK token, save and rebuild. The generated config is
   `build-reterminal-sticky/sdkconfig`, ignored by Git. Keep the token and built
   firmware private. Follow [credentials and backups](../../AGENTS.md#credentials-and-backups)
   to back up the device before its first flash.

3. Flash your backed-up Sticky using its console USB port:

   ```sh
   tools/board.sh reterminal-sticky flash PORT
   ```

   `PORT` is the CH343 console port for your device. Open Muse on your phone,
   select the advertised gadget, provide Wi-Fi and confirm pairing with AI.
   The on-device Settings keyboard remains available for Wi-Fi setup.

4. Copy the complete [SKILL.md](SKILL.md) into your Muse chat and ask Muse to
   install it, following the repository's [skill installation guide](../../skills/README.md).
   Ask Muse to create the lists, for example:

   > Create Shopping, Chores, To-do and Notes on the fridge.

   Muse uses `notes.set` with slugs `shopping`, `chores`, `todo`, `notes` and the
   corresponding display titles. Once the first list exists, it becomes home.
   Keep fewer lists if that suits your family.

5. Join Wi-Fi and allow time synchronization. Ask Muse to set `clock.set` with
   the current UTC epoch and your UTC offset in minutes for the local sync label;
   update that offset when daylight saving changes. Items added before valid time
   have `added_at: 0`.

## Things to say to Muse

- “Eggs are out, and get soy sauce.” → two Shopping items.
- “Add two cartons of milk and 500 grams of tomatoes.” → quantities stay in text.
- “Add Saturday cleanup to chores for Leo.” → text and `who: "Leo"`.
- “What's left on the fridge list?” → read incomplete items, with quantities and people.
- “Mark the eggs done.” → read the current IDs, then explicitly set `done: true`.
- “Clear the completed chores.” → remove completed Chores items.
- “Every Sunday evening, review what is left and ask us about old tasks.” → an
  optional Muse routine; agree its time, timezone and cleanup scope first.

Muse uses the device when reachable. While it sleeps, Muse reads the VM JSON as
its latest successfully mirrored snapshot and describes it as such. New edits
for a sleeping device are submitted when it is reachable again; the device is
the writer of the mirror file.

## At home

Hold AI, speak, release and read the short confirmation. Tap a row to tick or
untick it. Use tabs or swipe horizontally between lists; swiping left beyond the
last list opens Settings. Swipe right from Settings to return, or right from the
first list to see Muse's face. The Muse button also opens the face; swipe left
from it to return to lists. Vertical swipes and Previous/Next reach every row in
long lists. Clear done removes the current list's ticked rows. Up/Down wake the
screen, and normal battery sleep keeps the last e-paper image visible.

“Synced hh:mm” records the last completed upload in local time. An asterisk means
newer edits await upload; “Sync pending” means no timed upload has completed this
boot; “Synced --:--” means it completed before local time was available. “Save failed: retry” leaves the old data intact. “Busy: tap again” means
the local queue is full. Read the full text through Muse when a row is shortened.

## Commands and limits

All successful results contain `ok: true` and a `payload` with `version`,
`revision`, `next_id` and `lists`. Writes return the affected list;
`notes.get` returns all lists or one selected list. The mirror uses the same
schema and always includes every list. See [SKILL.md](SKILL.md) for parameters
and examples.

- List slugs: 1–23 lowercase ASCII letters, digits or hyphens; titles and people:
  1–31 UTF-8 bytes; item text: 1–127 UTF-8 bytes, single line. Multibyte characters
  use several bytes. Four lists and twenty items per list are enforced atomically.
- `notes.set` replaces one list. IDs are globally unique positive integers;
  generated IDs increase across restarts. Get IDs before toggling or removing.
  An explicit `done` value makes a retried checkbox command safe.
- Storage shares the existing NVS partition with settings and pairing. A full
  partition rejects the edit; keep item text concise. Partition tables are unchanged.
- VM mirroring is one-way and uses multiple writes for large JSON. A reader may
  encounter an incomplete file during an upload; retry a parse later. Interrupted
  uploads restart with overwrite, and “Synced” updates only after every chunk
  succeeds. The VM file can lag behind offline edits.
- Touch events retry until accepted by the shared event queue. They are RAM-only;
  lists remain in NVS, while an unsent event can be lost on power loss. Muse's
  response appears in your own chat; everyone with access to the fridge can see
  its lists. Use ordinary household notes rather than secrets.

## Host verification

From `esp32/` with the pinned FreeInk checkout already supplied by a Sticky build:

```sh
FREEINK_SOURCE_DIR=$PWD/managed_components/freeink-sdk python3 -m unittest discover -s tests -p 'test_*.py'
```

For just this application, use `-p 'test_sticky_notes*.py'`. The tests use real
cJSON, the production feature code and pinned FreeInk routing with hardware fakes.
