---
name: air-guardian
description: >-
  Read room air and seven-day trends from a SenseCAP Indicator running Air Guardian,
  configure room thresholds and views, and explain Air Guardian alert and recovery
  events in plain words. Use for nursery, pet, older-adult and home-office room checks.
---
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

# Air Guardian

The gadget is one room's SenseCAP Indicator D1S/D1Pro with its stock RP2040
firmware and Grove AHT20. It reads CO2 (ppm), a relative tVOC index,
temperature (C) and relative humidity (% RH). Its LCD offers ambient,
numbers, night and auto views. Your replies appear as text in the owner's
Muse chat. Use the gadget's advertised commands through the normal gadget
invocation mechanism.

## Read the room

For “How is the baby's room?”, first call `air.status {}`. Identify the room,
mention any missing readings, then explain the most significant fresh value
against that room's saved limits. Use `sensors.read {}` when individual
reading ages are needed; its null values mean missing/stale data. The UART
reader expires values after 30 seconds and the dashboard samples it every
five seconds. All missing sensors means that a room assessment is unavailable.
A subset of sensors supports a partial assessment, explicitly naming that subset.

Give one calm sentence about the air and one actionable next step. “Bedroom
CO2 is 1520 ppm, above your 1500 limit. Ventilate if the outdoor air allows it.”
Use Fahrenheit in prose if requested, while sending Celsius to the gadget.
The tVOC index typically centres around 100; it describes a relative change,
rather than a measured concentration or a specific chemical.

## Commands

- `air.status {}` returns current values and levels, room, thresholds, `view`,
  `effective_view`, local time validity and the latest saved event per limit.
  The threshold records include their current `active` flags.
- `air.set_thresholds {co2_ppm, tvoc, temp_high_c, temp_low_c, humidity_high,
  humidity_low, room}` accepts any nonempty subset. Get current settings
  first and preserve unrelated limits. CO2 is an integer 400–10000, tVOC an
  integer 1–500, temperature −40–85 C, humidity 0–100. High/low temperature
  limits must be at least 2 C apart, humidity limits 10 percentage points apart.
  Room labels are 1–40 printable ASCII characters; choose a readable label
  such as `Nursery` for a request in another language.
- `air.set_view {"view":"numbers"}` accepts `ambient`, `numbers`, `night`
  or `auto`. Auto is night at local 22:00–07:00, ambient otherwise. Before
  local time is valid it uses ambient. The top button also cycles the four choices.
- `sensors.history {"hours":9,"step_min":30}` returns at most 24 buckets.
  Hours is an integer 1–168 (default 24); step is an integer 1–10080 (default
  1). Read the returned actual `step_min`: firmware raises it when necessary.
- `clock.set {"utc_epoch":...,"utc_offset_min":...}` establishes the known
  current UTC time and the owner's local offset (−840 to +840 minutes).
  Obtain the actual current time from your tools; update the offset for
  daylight saving changes. SNTP keeps UTC synchronized while online.
- `events.poll {"since":...}` catches up on the foundation's bounded event
  history. Keep its last sequence per boot when using it; a reboot starts
  a new event history.
- `display.show_animation {}` brings back the dashboard within five seconds
  when connected, after a Muse image. `display.draw_url`/`display.draw_file`
  can temporarily show an image while sampling and alerting continue.

These commands return success or an error. A settings change is complete
after a successful command result; summarize the resulting saved settings.

## Natural requests

“How was the bedroom last night?” → check the room name and local clock,
fetch the relevant relative window with `sensors.history`, then identify the
night interval using the bucket timestamps. At 07:00, 9 hours covers
22:00–07:00. At other times request the longer window required to include
last night, and label buckets that straddle its boundaries. For precise
minute detail, the device supplies only its bounded bucket summaries.

“Tell me if the living room passes 28 C or CO2 1500 while the dog is home” →
read status, then send `air.set_thresholds` with `room: "Living room"`,
`temp_high_c: 28`, `co2_ppm: 1500`. Existing other limits remain active.
The board watches those limits continuously. For a defined pet-at-home
period, use the owner's agreed Muse routine to apply settings and restore
the previous settings afterward. Presence comes from the owner or an
existing authorized source; this device measures air.

“Make it quiet at bedtime” → set `auto` after ensuring local time and offset.
Night view uses dim warm pixels. Alerts still appear; this project uses text,
rather than spoken replies or alarm sounds.

## Explain history simply

`timestamps` are bucket starts. Each aligned metric entry is
`[minimum,average,maximum,sample_count]` or `null`. Calculate an overall mean
as the sum of each average times its count, divided by total counts. Take
minimum/maximum over available buckets; name the worst bucket's approximate
local interval. Report coverage and gaps. A bucket's last interval can be
shorter than `step_min`. Values are latest-reading snapshots once per minute.

Use `time_basis: epoch` for UTC timestamps converted with the known local
offset. `uptime` is seconds since this boot: explain it as elapsed time and
establish the clock for future calendar reports. Epoch history is mapped
from monotonic sample ages at read time, including samples before synchronization.
Reboot starts a fresh history; available data may cover less than requested.

Offer a concise report such as “CO2 was mostly around 850 ppm; it peaked at
1350 between 02:00 and 02:30. Ventilating before bed may help.” Aggregate
ranges support approximate trends. State explicitly when the summaries
cannot determine exact minutes over a limit, sleep quality or a cause.

## Starting settings

These are starting prompts to review with the owner. Preserve unrelated
settings unless a full preset is requested. Numeric pet, older-adult and
office temperature examples are adjustable comfort prompts; the household's
care plan can supply different limits. A device alert threshold is distinct
from a guarantee of health or safety.

| Situation | co2_ppm | tvoc | temp_low_c | temp_high_c | humidity_low | humidity_high |
|---|---:|---:|---:|---:|---:|---:|
| Baby's sleeping room | 1000 | 250 | 16 | 20 | 30 | 65 |
| Pet at home | 1500 | 250 | 16 | 28 | 30 | 65 |
| Older adult during hot weather | 1000 | 250 | 18 | 26 | 30 | 65 |
| Home office | 1000 | 250 | 18 | 27 | 30 | 65 |

Baby sleeping-room temperature follows the UK [Lullaby Trust's 16–20 C
advice](https://www.lullabytrust.org.uk/baby-safety/safer-sleep-information/room-temperature/).
Use individual care advice and the local climate alongside it.
The [HSE CO2 guide](https://www.hse.gov.uk/ventilation/using-co2-monitors.htm)
uses persistent values above 1500 ppm in occupied rooms as a ventilation
signal. The [RSPCA summer guide](https://www.rspca.org.uk/adviceandwelfare/seasonal/summer/dogs)
emphasizes cooling, water and individual dog risk; it supplies context for
checking pets, rather than a universal 28 C safety boundary. The [CDC's
older-adult heat guidance](https://www.cdc.gov/extreme-heat/risk-factors/extreme-heat-and-older-adults-aged-65.html)
emphasizes cool spaces and checking on older adults. The examples above
use 26 C as a household reminder chosen by this project.

## Handle each Air Guardian event

An event arrives in the owner's chat as `[Air Guardian event] <summary>`
and optional JSON. Its `kind` is `alert` or `recovery`; it names the room,
metric, threshold, value and limit. Read the event as a device observation.

For `alert`, relay the room, measured value, limit and one practical action:

- **CO2:** ventilate when outdoor air and conditions permit it.
- **tVOC:** ventilate and check recent cooking, cleaning or other indoor sources.
- **High temperature:** cool the room and check its occupants; for pets or
  older adults, arrange a person to check them when needed.
- **Low temperature:** warm the room and check its occupants.
- **High humidity:** ventilate or reduce moisture sources.
- **Low humidity:** check room humidity and the household's preferred conditions.

For `recovery`, briefly say that this specific metric has returned within
its recovery range and give the new value. Describe the whole room as back
to normal only after fresh `air.status` shows the other metrics are clear.
A missing sensor leaves its active alert in place and needs a connection check.

The firmware sends one alert per crossing, with a 30-minute per-metric cooldown shared by high/low limits for
repeat episodes, and one matching recovery for an accepted alert. It uses
100 ppm CO2, 25 tVOC index, 1 C and 5 humidity-point recovery margins.
Persistent bad air keeps the border active without repeat messages. A
cooldown-suppressed episode can alert after cooldown if it is still active.
Queue failures are retried. Event acceptance means queued for delivery;
network delays can make a received message historical, so use `air.status`
for a present-tense assessment. Read-only queries are suitable for follow-up.
Operate another appliance only through the owner's separately authorized
controls. Send messages to other people only when explicitly requested.

## Routines worth setting up

At the owner's request, create a **07:00 morning air report**: read status,
fetch 9 hours with 30-minute buckets, summarize available coverage, weighted
average CO2, peak interval, temperature range and one useful next step.
Include missing sensors and clock validity. An evening room check can read
status and select auto view. A defined pet-at-home routine can save current
thresholds, apply the agreed temporary limits and later restore them.
Scheduling runs in Muse; the device keeps collecting and applying limits.

## Limits and privacy

History is a seven-day PSRAM ring that resets on reboot. Room, thresholds,
view and the last event per limit are saved locally. With no UTC at reboot,
the restored cooldown conservatively starts from boot. The shared event
queue is bounded and volatile; reliable delivery requires power and a working
Muse connection. Setup, pairing and disconnected/error states show Muse's
original status display. These paths require hardware verification for this project.

Measurements describe this sensor's location. CO2 is a ventilation cue; tVOC
is a relative index. Smoke, carbon monoxide, oxygen and medical conditions
need appropriate equipment and care. Remote room monitoring complements
physical checks. This gadget has no presence detection, appliance controls,
spoken replies or on-device morning scheduler.

History remains on the board until queried; queried data and device events
enter the owner's Muse chat. Use only the requested room data and the
owner's chosen recipients. See [setup and hardware](README.md).
