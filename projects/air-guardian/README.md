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

[中文](README.zh-CN.md) · [Muse skill](SKILL.md) · [Validation](VALIDATION.md)

A calm air display at home and a room sentinel in your own Muse chat. For a
nursery, a pet's living room, an older relative's home or a home office, it
turns readings into a glanceable answer and a practical next step. Ask from
anywhere your Muse is available: “How is the baby's room?” or “How was the
bedroom air last night?”

## Hardware

Use a **SenseCAP Indicator D1S or D1Pro**, USB power and Wi-Fi. Keep Seeed's
stock RP2040 firmware installed and connect the supplied **Grove AHT20** for
temperature and humidity. CO2 and the tVOC index come from the onboard
SCD41 and SGP40 through that RP2040. D1 and D1L have no built-in air sensors.
Air Guardian reuses the existing UART reader, LCD pin map and top button.

See [Seeed hardware](https://wiki.seeedstudio.com/SenseCAP_Indicator_Dive_into_the_Hardware/)
and [RP2040 source](https://github.com/Seeed-Solution/SenseCAP_Indicator_RP2040).
Place the device in the room being watched, with air circulating around it,
away from direct sunlight, heat sources and someone's exhaled breath.

## Build, install and pair

From the repository's `esp32/` directory, using ESP-IDF **v6.0.1**:

```sh
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf6.0_py3.14_env
. ~/esp/esp-idf/export.sh
tools/board.sh sensecap-indicator build
idf.py -B build-sensecap-indicator menuconfig
tools/board.sh sensecap-indicator build
```

In menuconfig set your private SDK token under **ESP32 Device SDK**. Obtain
it from Muse's **Account > SDK tokens**; the build's ignored `sdkconfig`
holds it. The Indicator overlay enables `HOMEHUB_AIR_GUARDIAN`, its sensor
reader, device events, local clock and shared card assets. Other overlays
keep Air Guardian off. Existing generated configurations keep their previous
values; enable Air Guardian in menuconfig when reusing one.

Before the first installation, back up the board using the [repository's
backup instructions](../../AGENTS.md#credentials-and-backups). Then run:

```sh
tools/board.sh sensecap-indicator flash
```

Open Muse, add the gadget, supply Wi-Fi and press the top button to confirm
pairing. The [beginner's guide](../../README.md) covers SDK tokens and setup.
Once connected, the dashboard appears. Setup, pairing, connection loss and
errors use the existing Muse status screen.

Give Muse [SKILL.md](SKILL.md), or its raw contents, and ask it to install
this as your **Air Guardian** skill. Then ask Muse to set the correct current
UTC time and your local UTC offset with `clock.set`. SNTP synchronizes UTC;
the stored offset supplies local time, including the night schedule. Update
the offset when your daylight saving time changes.

## At home

A short top-button press cycles **ambient → numbers → night → auto** and
saves the choice. Pairing confirmation and the setup-window button action
remain available during setup; the five-second reset hold retains its setup
role. The reset clears pairing and Wi-Fi; device settings remain stored.

- **Ambient:** room name, a plant inside a green/yellow/orange/red air ring,
  a mood and CO2. Colour follows the worst available metric relative to the
  configured limits; missing readings are explicitly identified.
- **Numbers:** four values with units and 24 hourly CO2 averages. Gaps mean
  missing data. The graph scales to the observed range.
- **Night:** a dark background and dim warm CO2 text. `auto` uses this view
  from 22:00 inclusive until 07:00 exclusive, otherwise ambient. Before local
  time is valid, auto uses ambient.
- **Alert:** a border pulses once a second and gives advice until recovery.
  Night alerts use a dim warm border. Display brightness is reduced through
  pixel colours; the existing backlight driver remains in use.

Muse can still show an image with `display.draw_url` or `display.draw_file`.
`display.show_animation` returns to the dashboard within five seconds while
connected. Sampling and alerts continue while an image is displayed.

## Things to say to Muse

- “Name this room Bedroom, and warn me above 1000 ppm CO2.”
- “Tell me if the living room exceeds 28 C or CO2 1500 while the dog is home.”
- “Show the four readings.” / “Use night mode.” / “Use automatic night mode.”
- “How was the bedroom air last night? Explain the worst period simply.”
- “At 07:00 each morning, tell me how the bedroom air was overnight.”

Room labels use 1–40 printable ASCII characters, such as `Nursery`. One board
watches one room. Limits remain active continuously; Muse routines can apply
and later restore agreed settings for a pet-at-home period. Presence sensing
and a morning scheduler are provided by the user's Muse routines.

## Commands and data

All commands return the usual `ok` and `payload` or `error` result. Work runs
on an application task, including NVS writes and history aggregation.

| Command | Parameters and result |
|---|---|
| `sensors.read` | Existing current readings with units and ages; stale readings are null. |
| `air.status` | No parameters. Current values/levels, room, configured/effective view, time validity, limits and the last saved event per threshold. |
| `air.set_thresholds` | Any nonempty subset of the limits below and/or `room`; saves atomically. |
| `air.set_view` | Required `view`: `ambient`, `numbers`, `night`, `auto`; saved. |
| `sensors.history` | Optional integer `hours` 1–168, default 24, and `step_min` 1–10080, default 1. Actual step increases to keep at most 24 points. |
| `clock.set` | Integer `utc_epoch` and `utc_offset_min` (−840 to +840). |
| `events.poll` | Optional `since`; foundation event backlog for catching up. |

| Limit | Firmware default | Accepted range | Recovery margin |
|---|---:|---:|---:|
| `co2_ppm` | 1000 | Integer 400–10000 ppm | 100 ppm below |
| `tvoc` | 250 | Integer 1–500 index | 25 below |
| `temp_high_c` | 28 | −40–85 C | 1 C below |
| `temp_low_c` | 16 | −40–85 C | 1 C above |
| `humidity_high` | 65 | 0–100 % RH | 5 below |
| `humidity_low` | 30 | 0–100 % RH | 5 above |

High and low temperature must be at least 2 C apart; humidity limits at least
10 percentage points apart. A crossing means strictly above/below the limit.
Recovery includes the margin boundary. Each limit sends one alert, then one
recovery for an accepted alert. Repeated crossings of the same metric have a 30-minute alert
cooldown shared by its high and low limits. An ongoing alert sends no periodic reminders. A crossing suppressed
by cooldown can alert later if still active; an episode that recovers before
being announced produces no recovery message. Missing readings retain an
existing alert and show a sensor-connection instruction.

Settings and the last alert per limit survive reboot in NVS. If UTC is
unavailable at boot, a restored alert cooldown starts conservatively from boot.
History lives in approximately 315 KiB of PSRAM and restarts empty on reboot.
The latest fresh value of each metric is sampled once a minute for seven days;
these are minute snapshots, rather than averages of every UART reading.

History returns aligned `timestamps` and arrays `co2`, `tvoc`, `temperature`,
`humidity`. Each metric entry is `[min,avg,max,count]` or `null`; timestamps
are bucket starts. Use sample counts to weight averages. `step_min` is the
actual returned width. Timestamps use `time_basis: epoch` when UTC is valid,
otherwise `uptime`; epoch timestamps map monotonic sample ages onto the
current UTC clock. A clock correction changes that mapping consistently.
The final bucket may be shorter. Bucket summaries support trends and peaks;
exact time above a limit cannot be recovered from them.

Events arrive as `[Air Guardian event]` with short advice and JSON `kind`
(`alert` or `recovery`), metric, threshold, room, value and limit. They enter
the owner's Muse chat through the shared [event foundation](../../esp32/docs/gadget-foundation.md).
Delivery depends on power, Wi-Fi and Muse connectivity. The foundation's
pending queue and delivery backlog live in RAM and are bounded.

## Practical starting settings

The [skill](SKILL.md#starting-settings) has copyable presets. Choose the room
and care plan together: a baby's sleeping room starts at 16–20 C per the
[Lullaby Trust](https://www.lullabytrust.org.uk/baby-safety/safer-sleep-information/room-temperature/);
pet, older-adult and office temperature limits are adjustable comfort prompts,
rather than universal health limits. CO2 describes ventilation: the
[HSE guide](https://www.hse.gov.uk/ventilation/using-co2-monitors.htm) identifies
persistent readings above 1500 ppm in occupied rooms as a reason to improve it.
The tVOC number is a relative index, with 100 typical; it is not a ppm reading
or a pollutant identification.

Use the display as a prompt to check the room and its occupants. This is a
comfort monitor: smoke, carbon monoxide, oxygen, medical conditions and pet
presence require their own appropriate equipment and checks. Opening a window
is suitable when outdoor air and conditions allow it. There is no automated
window, heater or fan control, and no spoken reply or chime in this project.

Room history stays on the board until requested; requested data and alerts
enter your Muse chat. Keep the board in the intended room and share its
history only with the people you choose.

## Verify

From `esp32/`:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
CARD_PREVIEW_DIR=/tmp/air-preview python3 -m unittest tests.test_air_guardian
```

The second command writes `air_ambient.ppm`, `air_numbers.ppm`,
`air_night.ppm`, `air_alert.ppm`. It tests real C code with memory/undefined
behaviour checks, including storage rollover, bucketing, alarm transitions,
parameter validation, settings restore, asynchronous results and registration.

To regenerate shared assets from the repository root:

```sh
CARD_ICON_FONT=$HOME/Desktop/muse-gadget-challenge/.cache/fonts/MaterialSymbolsRounded.ttf \
  $HOME/Desktop/muse-gadget-challenge/.venv/bin/python esp32/tools/gen_card_assets.py
```

Validation for this implementation is host-side. Verify a fresh Indicator
build and app size on an unrestricted host, then on one D1S/D1Pro test pairing,
reconnect, top-button reset/confirmation/view cycling, sensor removal, all
thresholds and recovery, reboot persistence, local night transitions, images
and dashboard return. Check RGB colours, physical night visibility, border
pulse and seven-day memory use on the actual LCD. Record revision, board
revision, unit count and results with those checks.
