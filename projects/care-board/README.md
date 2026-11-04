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

# Care Board for reTerminal E1001

A calm kitchen screen for an older parent, connected to a family member's Muse.
Send a warm message from anywhere, set a daily reminder, and hear back when the
parent presses a key. Large text and a retained e-paper image keep the day visible.

[简体中文](README.zh-CN.md)

## Hardware

Use a Seeed reTerminal E1001 with an 800×480 monochrome UC8179 display, Wi-Fi,
the onboard SHT4x temperature/humidity sensor and PCF8563 clock.

| Control | Action after setup |
|---|---|
| Green key, GPIO3 | Confirm the oldest due or missed reminder; otherwise read the newest unread message |
| Left white key, GPIO4 | Tell Muse “I'm OK today” |
| Right white key, GPIO5 | Ask the family contact to call |
| Green LED, GPIO6 | Blinks while a reminder is due |
| Buzzer, GPIO45 | Gentle reminder pattern and a brief accepted-key tone |

Keys and the LED are active low. The RTC is at address `0x51` on I2C0, SDA19/SCL20,
shared with the SHT4x. Pins and levels follow the Seeed E1001 V1.2 schematic and
the vendor-contributed [Zephyr board definition](https://github.com/zephyrproject-rtos/zephyr/tree/main/boards/seeed/reterminal_e1001).
Implementation references are recorded beside the constants in
[`reterminal_io.c`](../../esp32/main/reterminal_io.c).
Green retains pairing confirmation and setup; holding it for five seconds resets setup.
The care actions become available after setup, while pairing confirmation is idle.

## Build, install and pair

1. Install and activate **ESP-IDF v6.0.1**, following the
   [beginner's guide](../../README.md#option-c-build-it-yourself).
2. From this checkout's `esp32/` directory, build:

   ```sh
   tools/board.sh reterminal-e1001 build
   ```

3. Add your SDK token to this board's generated build settings using
   [Add your SDK token](../../README.md#4-add-your-sdk-token), then repeat the build.
4. Back up the device's existing firmware using the repository's board guide,
   connect the E1001's console USB, then install:

   ```sh
   tools/board.sh reterminal-e1001 flash
   ```

5. In the Muse app, add the advertised `MuseGadget-Disp-…`, supply home Wi-Fi and
   confirm pairing with Green, as described in
   [Add the device in the Muse app](../../README.md#step-3-add-the-device-in-the-muse-app).
6. Open [`SKILL.md`](SKILL.md) and paste its complete contents into the family's
   Muse chat. Ask Muse to use it with this E1001. For first use, give the parent's
   name, the family contact and the local time zone; Muse sets UTC and the offset.

The E1001 overlay enables Care Board and its shared foundations, uses size optimization
and compressed card masks in the fixed app slot. Build settings
live in `build-reterminal-e1001/`. The same checkout supplies the firmware,
commands and skill. An already provisioned board retains its pairing during updates.

## Things to say to Muse

- “Set this Care Board up for Dad. Tell Mia when he confirms. His local time is London time.”
- “Remind Dad about his blood pressure pill at 8 each day. Tell me if he hasn't confirmed by 8:30.”
- “Tell Dad I'll be home for dinner at seven. Love you!”
- “The carer comes tomorrow morning at 10. Put that on the board once.”
- “Show today's weather and remind Dad to take an umbrella.”
- “Has Dad confirmed his reminder?”

Muse uses `care.update` for a patch and `care.state` for the current facts.
A message sent with `add_message` appears as NEW and plays one soft chime.
At the reminder time the page shows “Time for …”, the LED blinks and the buzzer
plays a short pattern. It repeats after five and ten minutes, at most three times.
Without confirmation, the reminder becomes MISSED after its escalation interval.

Green confirms the oldest due or missed reminder. With none waiting, it marks
the newest unread message as read. Another Green press displays “Nothing to confirm”.
Repeated presses of the same key within two seconds are ignored. Accepted keys beep.

The family's Muse receives chat events such as:

> Dad confirmed "Blood pressure pill" (due 08:00) at 08:03.
>
> Dad has not confirmed "Blood pressure pill" 30 minutes after 08:00.
>
> Dad checked in: I'm OK today.
>
> Dad asked Mia to call.

The screen acknowledges a confirmation with a check and “Told Mia at 08:03”,
or a call request with “Asked Mia to call you”. Muse relays missed reminders and
call requests promptly and suggests a phone call. Account notification settings
control how Muse chat reaches the family's phone.

## Commands and stored state

| Command | Use |
|---|---|
| `care.update` | Patch person/family/title/weather; replace messages/reminders, prepend `add_message`, or set `now_epoch` with `utc_offset_min`; returns state |
| `care.state` | Full model, reminder status and times, key/check-in history, clock validity, RTC/storage flags and cached indoor readings |
| `care.clear` | Show the normal status page and retain the care model; the next `care.update` opens Care Board again |

A reminder has `id`, `time` (`HH:MM`), `label`, optional card `icon`, `repeat`
(`daily` or `once`) and `escalate_min` (1–1440, default 30). The same ID and time
retain their status when a reminder list is replaced. A newly set time that has
already passed today is pending for tomorrow. “Tomorrow at 10” fits this path
when today's 10:00 has passed; use a Muse scheduled action to install a future
one-time reminder at the intended date when today's 10:00 is still ahead.

At local midnight, daily reminders reset and finished one-time reminders drop.
A daily occurrence's confirmation window ends at that midnight reset. Choose a
reminder time and escalation interval that fit within the same local day.
One-time reminders retain their scheduled occurrence until it finishes.
Clearing the page keeps reminders running; replace `reminders` with `[]` to
finish the current reminder plan.

Settings, messages, reminder runtime status and key history are kept in a
versioned, checksummed NVS blob. A valid RTC restores UTC after a power cut;
SNTP and Muse time updates write the RTC. The local UTC offset is stored separately.
Muse can refresh the offset after daylight saving changes.

## Limits and daily use

- This is a family reminder and communication aid, **not a medical device**.
  A Green confirmation records a key press; use a conversation with the parent
  to establish whether medication was actually taken. Urgent health concerns
  need a direct call or appropriate local help.
- The UI and font support English/ASCII plus `° • – — ’ “ ” …`.
  Names: person 31 bytes, family/from 39, title 55. Messages: 191 bytes each,
  at most three. Reminder IDs: 31 letters/digits/`_`/`-`; labels: 63 bytes,
  at most eight reminders. The page shows the newest message and up to four reminder rows. A two-line
  message leaves space for three rows. The black date band, inverted NEW/MISSED
  tags and 42-pixel checkboxes remain clear at a distance; finished rows show
  a check and the local confirmation time with the filled icon omitted.
- Weather is supplied by Muse; temperature is Celsius. Weather text holds 87 bytes;
  icon names come from [`gen_card_assets.py`](../../esp32/tools/gen_card_assets.py).
- Wi-Fi and a live Muse session deliver events. Queued events live in RAM;
  `events.poll` can catch up from the latest 32 events while the same boot is running.
  Power loss clears that event history, while the care model remains stored.
- E-paper full refresh flashes and takes about two seconds. Pages refresh on
  visible state changes and at midnight; indoor readings appear at that refresh.
- The RTC backup supply and home Wi-Fi should be checked as part of household setup.

A useful morning routine is to ask Muse to update weather and the day's plan at
07:00 in the parent's local time, using Muse's scheduling capability.

## Host checks and previews

From `esp32/`:

```sh
python3 -m unittest tests.test_care_board tests.test_reterminal_io
CARD_PREVIEW_DIR=/tmp/card-preview python3 -m unittest tests.test_care_board
```

The preview command writes `care_day.pgm`, `care_due.pgm`, `care_missed.pgm` and
`care_done.pgm`.
Host checks exercise scheduling, command validation, persistence, rendering,
key gating, reset compatibility, LED/buzzer timing and shared RTC I2C access.
On one E1001 running revision `4623eb6`, Muse updates displayed clock, weather,
message and a test reminder. The due beep/highlight and Muse event, a missed event
with a two-minute interval, and a Green confirmation with check/footer/Muse event
were verified. The current reply matching, bus initialization, compressed assets,
size optimization and page presentation have host coverage and await device checks.
RTC backup and both white keys also await device checks.

`link.care` logs report reminder IDs/status changes, key action results, event
types/sequences and redraw requests with timestamps. Link console diagnostics can
be enabled with `>event.debug=1` and ended with `>event.debug=0`; they report
request/row metadata, remain in RAM and start disabled after boot. Subscribe frame
summaries (`@event.frame`) aggregate byte/newline counts, pending bytes and whether
the pending row contains only whitespace, at most once per second. Row diagnostics
include the root `seq`, status-event `activity_code` and `payload_status` (the
payload's `status` string); the existing `status` field is the HTTP status.
`dropped: "seq"` marks a repeated sequence among the most recent 64 distinct values.
An unseen sequence is accepted in any arrival order. Each turn ends with one
`@event.state` summary of matching flags, candidate ID tails/lengths and timing.
A reply settles after all its messages finish and matching or sessionless event
rows remain quiet for three seconds. A busy agent holds it until at most 20 seconds
after the last content row; later content restarts that hold. Whitespace keepalives
preserve quiet time. An unfinished turn retains its 90-second timeout.
