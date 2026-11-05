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

# Care Board validation

Tested on 2026-11-04 at commit `29b7eb7` (branch `project/care-board`).
Hardware: **one** Seeed reTerminal E1001 (ESP32-S3, 32 MB flash, CH340 console),
paired with Muse over home Wi-Fi. Toolchain: ESP-IDF v6.0.1.

## On the device

The family member talked to Muse in the Muse app; Muse used the board's commands.

| Check | Result |
|---|---|
| Boot | Shared I2C bus starts cleanly; SHT4x reads 24.7 °C / 55.6 %RH on the first poll |
| “Leave Dad a message and add a reminder in two minutes” | Muse called `care.state`, then `care.update`; the page redrew with the message and the reminder row |
| Reminder time | Page shows the due reminder, buzzer sounds, `due` event reaches the family's Muse chat |
| Green pressed 5 s after the alarm | Row ticks and shows the confirmation time; `ack` event posted at once |
| Muse reply to `ack` (skill installed) | Muse told the family that Dad confirmed the reminder, about 3 s after the event |
| Reminder left unconfirmed (commit `4623eb6`) | `missed` event after the escalation interval; Muse notified the family |
| Device event to Muse and reply capture | `>event=` posts with HTTP 200; the board captures Muse's reply text |
| Silent Muse turn | A `due` event Muse chose not to answer releases the queue; the following `ack` is posted without waiting |

Muse shows care text in English on this board, which carries the Latin font set.

## Host tests

From `esp32/`:

```sh
python3 -m unittest tests.test_gadget_events tests.test_care_board tests.test_reterminal_io tests.test_epaper_cards tests.test_gadget_clock
```

20 tests, all passing. The tests cover the reminder model and schedule, key actions,
NVS storage, shared I2C start-up order, card rendering and asset decoding, the
reply stream (echo with an independent `seq` counter, split rows, whitespace,
busy hold) and silent turns.

## Build

`tools/board.sh reterminal-e1001 build`: app `0x1a1000` bytes in the fixed
`0x200000` slot, **19 % free**. The partition table is unchanged.

## CodeCraft Hub draft flash (2026-11-05)

The uploaded release `muse-gadget-care-board-4dda0ae.bin` (token-free, built from `4dda0ae`) was flashed from the draft page of Hub app 13464 onto one reTerminal E1001. After a reset the board started normally and the owner's device checks passed. Hub writes the application image only, so the token and pairing already stored on the board carried over. The draft page showed no reset or token prompt ([sensecraft-cli#6](https://github.com/Seeed-Solution/sensecraft-cli/issues/6)).

## Awaiting checks

- `read`, `checkin` and `call_request` events with the skill installed.
- Overnight operation, Wi-Fi reconnection with queued events, and the daily reset.
- Hub installation on a board without a stored token, with the token entered through the page or USB serial.
