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

# Kid Board — a child's day

A colourful timeline and star chart on the **reTerminal E1002 only** (800×480,
E Ink Spectra 6). A parent sends plans and encouragement through Muse. The child
presses Green after each step, and Muse reports the confirmation to the owner.

[简体中文](README.zh-CN.md) · [Muse skill](SKILL.md) · [Validation](VALIDATION.md)

## Build, install and pair

1. Activate **ESP-IDF v6.0.1** using the [build guide](../../README.md#option-c-build-it-yourself).
2. From `esp32/`, run `tools/board.sh reterminal-e1002 build`.
3. Add the account's SDK token using `idf.py -B build-reterminal-e1002 menuconfig`
   (ESP32 Device SDK → SDK token), then build again. Settings stay in the ignored
   `build-reterminal-e1002/sdkconfig`; provision private values locally.
4. Back up the device's original flash following [the board guide](../../esp32/AGENTS.md#flash),
   then install with `tools/board.sh reterminal-e1002 flash`.
5. Add `MuseGadget-Disp-…` in the Muse app, enter Wi-Fi and confirm pairing with Green.
   Paste [SKILL.md](SKILL.md) into the owner's Muse chat and identify this E1002.
6. Give Muse the child's name, local time zone, permitted star givers, daily star
   cap and agreed reward. Muse uses `clock.set` to set UTC and the local offset.

`HOMEHUB_KID_BOARD` selects the shared clock, RTC/keys/buzzer, card drawing and
Muse event service. The E1002 overlay enables it with size optimization.
The E1001 retains Care Board. The fixed dual 2 MB application slots and partition
table are shared with the CodeCraft Hub flashing layout.

## Talk to Muse

- “Set up Mia's Kid Board. Grandma and I may give up to three stars a day;
  five stars earns a park picnic.”
- “Swimming is cancelled today; piano at 4 instead, Grandpa picks her up.”
- “Mia finished her homework, give her a star.”
- “Tomorrow morning: 7:00 wake up, brush teeth, get dressed, breakfast,
  7:40 leave. She presses the button after each.”
- “What did Mia confirm today? How many stars does she have?”

Muse reads `kid.state`, changes the complete schedule while retaining stable IDs,
and sends it with `kid.set_schedule`. For stars it reads the total, checks the
owner's rules, and writes the new absolute total with `kid.set_stars`.
The supplied skill describes these rules and an evening/weekly review.

## Keys and screen

| Control | Action after setup |
|---|---|
| Green, short press | Confirm the highlighted step: the latest due unfinished step, otherwise the next step today |
| Green, pairing | Confirm pairing; the Kid Board action becomes available once setup and pairing confirmation finish |
| Green, five-second hold | Reset device setup through the existing SDK flow |
| White keys | The timeline remains on screen |
| Buzzer | One short accepted-confirmation tone |

A confirmation adds a large green check and posts `kid.done {id, at}` through
`gadget_events`. Muse receives the child's name and step label as notice text
and reports it to the owner. A failed event enqueue leaves the step ready for
another press. Accepted Green presses have a two-second guard. Stars come from
the parent's agreement; a key confirmation changes the step only.

The header shows the child, today's weekday/date and plan date.
Up to ten cards occupy two rows. Yellow and a larger icon identify the current
step; completed cards carry green checks. The highlight follows the latest due
unfinished step, or the next step today when none is due. Simultaneous steps
retain the parent's order. The bottom holds up to twenty stars,
the total and goal, reward and one-line parent note. `count >= goal` shows
“Reward earned!”. Long text is fitted or ends with an ellipsis.

Six-colour panel refresh takes about 30 seconds. Changes take effect in state
immediately after persistence; drawing requests merge and panel redraw starts
are at least 60 seconds apart. The first update after an idle window can draw
immediately. Updates follow schedule/stars/note edits, completion changes, a
change of the highlighted step and the local date. The worker sleeps until the
next actual step/date boundary, a command/key, a clock correction or a pending
storage/drawing retry. The header retains its weekday and date between content
changes. A command that leaves stored content unchanged schedules no redraw and
writes no NVS data. The visible picture can lag during a panel refresh.

## Commands and limits

| Command | Parameters |
|---|---|
| `kid.set_schedule` | `items` (0–10), optional `child`, optional `date` |
| `kid.set_stars` | Required absolute `count` and `goal`; optional `reward_label`, `reward_icon`, `note` |
| `kid.mark` | Existing `id` and Boolean `done`; returns resulting state |
| `kid.state` | `{}`; schedule with `done`/`done_at`, `current` or null, stars/reward, `clock_valid`, UTC/local offset and storage/RTC flags |

An item contains `id` (1–31 ASCII letters/digits/underscore/hyphen), `time`
(`HH:MM`), `label` (1–47 bytes), `icon` (1–31 letters/digits/underscore/hyphen),
and optional `who` (0–31 bytes). IDs are unique. Cards sort by time and retain
input order for simultaneous steps. Use short words for the clearest display.

`child` is 1–31 bytes. `date` is a real local `YYYY-MM-DD` date in 2024–2099;
when omitted it means today and requires a valid clock. On the same plan date,
matching IDs retain completion even when a time or label changes. A different
plan date clears completion. A future plan waits until its date for Green
confirmation. At local midnight, an earlier plan advances to today and resets
its steps; the last plan repeats until Muse replaces it. An evening update for
tomorrow replaces the visible plan immediately. Use a different ID for a new
occurrence that should begin unfinished on the same day.

Stars persist across days: `count` 0–999, `goal` 1–20, `reward_label` 0–63 bytes,
`reward_icon` 1–31 letters/digits/underscore/hyphen, `note` 0–95 bytes.
Omitted optional star fields retain their values; empty label/note clears them.
The firmware stores absolute totals; Muse manages permissions, daily caps and
reward agreements. `kid.mark` is a remote correction and sends no physical-key
event. With an invalid clock a remote completion stores `done_at: 0`.

Text supports English/ASCII and the generated card punctuation, including curly
quotes, dashes, degree sign and ellipsis. Available friendly icons include
`wake`, `sun`, `toothbrush`, `shirt`, `breakfast`, `bowl`, `backpack`, `school`,
`homework`, `book`, `swim`, `piano`, `music`, `bath`, `bed`, `sleep`, `play`,
`park`, `car`, `pickup`, `star`, `gift`, `reward`. Unknown icon names show the
item's text label; an unknown reward icon shows “Gift” with the reward label.
Masks are tinted using the panel's six native inks.

Schedule, completions, stars and note survive reboot in a versioned, checksummed
NVS record. A valid PCF8563 RTC restores UTC after restart; SNTP and `clock.set`
update it. Muse refreshes the local offset when daylight saving time changes.
Commands report storage errors; a key confirmation retained in RAM retries NVS
within a minute. Pending events are held in bounded RAM by the shared service,
so owner delivery depends on connectivity and power; `events.poll` can catch up
on its last 32 retained notices. A confirmation records a button press.

## Check the implementation

From `esp32/`:

```sh
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf6.0_py3.14_env
python3 -m unittest tests.test_kid_board tests.test_care_board tests.test_epaper_cards tests.test_gadget_events
CARD_PREVIEW_DIR=/tmp/card-preview python3 -m unittest tests.test_kid_board
tools/board.sh reterminal-e1002 build
tools/board.sh reterminal-e1001 build
```

The preview run writes `kid_morning`, `kid_afternoon_changed`, `kid_done` and
`kid_stars_goal` as PPM and PNG using the production renderer and six exact RGB
ink values. [VALIDATION.md](VALIDATION.md) records the tested revision, build
sizes, host results and hardware checks.
