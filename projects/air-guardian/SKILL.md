---
name: outdoor-air
description: >-
  Look up outdoor air for the owner's place with Muse tools, then push an AQI
  or PM2.5 snapshot and short advice onto a SenseCAP Indicator D1. Read saved
  observation age and select ambient, numbers or night views.
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

# Outdoor Air

Supported hardware: **SenseCAP Indicator D1 only**, Link-only profile,
3.95-inch LCD. D1S and D1 Pro are outside this skill's support scope.
D1 has no onboard CO2/tVOC sensors; this application uses no indoor sensors,
Grove AHT20 or RP2040 sensor UART. Outdoor air comes from Muse's own tools.

## Refresh the home glance

1. Resolve the owner's place and preferences from authorized context. Ask for
   a location only when it is unavailable. Use a city label on screen; keep
   precise location details in the owner's private context.
2. Look up current outdoor air using your available tools. Keep the provider,
   observation time, units, averaging period and AQI standard clear. The
   gadget accepts US EPA AQI; use a provider that supplies that scale, or
   supply PM2.5 with its corresponding US EPA category from the source.
3. Read `air.status {}`. Compare its observation time with the lookup. Keep
   the newest observation for the chosen place.
4. Set the actual current clock and local offset with `clock.set` when needed.
   Send one complete `air.update` payload using the source observation UTC,
   an English/transliterated place label and a brief ASCII advice line.
5. After successful command completion, briefly tell the owner the place,
   result and observation time in their language. On error, report the error
   and leave the last good snapshot available for viewing.

For an unavailable or stale provider report, explain the available age. Refresh
from a suitable source when possible. Advice should reflect the source and
owner's preferences. Repeat pushes of a report retain its observation timestamp.

## Command contract

Use the normal advertised gadget invocation mechanism.

- `air.update {aqi?, pm25?, category, advice, place?, updated_utc}` replaces the
  whole snapshot. At least one metric is required; missing/null metrics remain
  absent. AQI: integer 0–500, **US EPA scale**. PM2.5: finite 0–1000 **ug/m3**.
  `advice`: 1–80 printable ASCII characters, ideally about 25 for a short line.
  `place`: optional 1–40 printable ASCII characters. `updated_utc`: integer
  observation Unix seconds 1704067200–4102444799. Fields are case-sensitive.
  Older observations and observations more than five minutes ahead of a valid
  device clock fail validation. Unknown/duplicate fields fail validation.
- Categories are `good` (AQI 0–50), `moderate` (51–100),
  `unhealthy_sensitive` (101–150), `unhealthy` (151–200),
  `very_unhealthy` (201–300), `hazardous` (301–500). An AQI and its category
  must match. For a PM2.5-only report, use the source's matching US EPA category
  with its observation period. Source: [AirNow AQI basics](https://www.airnow.gov/aqi/aqi-basics/).
- `air.status {}` returns `source: muse`, `aqi_scale: US EPA`,
  `pm25_unit: ug/m3`, `snapshot`, `age_s`, `clock_valid`, `refresh_needed`,
  and `view`. `snapshot: null` means awaiting the first update; `age_s: null`
  means observation age is unknown. `refresh_needed` is true when age is
  unknown or at least three hours. Read-only queries keep saved state intact.
- `air.set_view {"view":"night"}` saves `ambient`, `numbers` or `night`.
  The top button cycles these three choices. Settings and snapshots persist.
- `clock.set {"utc_epoch":...,"utc_offset_min":...}` sets the actual current
  UTC time and local offset (−840 to +840 minutes). UTC is also synchronized
  online by the shared clock service. Update the offset for daylight saving.
- `display.show_animation {}` returns from a temporary Muse image to the
  advisor within five seconds while connected.

Example payload shape; substitute real observations and time:

```json
{
  "aqi": 78,
  "pm25": 24.6,
  "category": "moderate",
  "advice": "Check your local forecast.",
  "place": "Home city",
  "updated_utc": 1793793600
}
```

The two numbers can describe different reporting periods; describe each using
its provider metadata. Firmware checks their ranges and the AQI/category pair;
source interpretation belongs to Muse.

## Natural requests and routines

“Can we go outside?” → look up current outdoor air for the owner's place,
update the gadget, then offer one brief source-grounded suggestion that accounts
for the owner's preferences.

“Show the numbers” → `air.set_view {"view":"numbers"}`. “Make the screen calm
at bedtime” → select `night`. Night dims rendered pixels; the board's backlight
setting stays in effect.

At the owner's request, schedule a morning refresh in Muse: query the provider,
set the current local clock if needed, push a new observation and summarize it.
Scheduling belongs to Muse. The gadget retains its last snapshot between updates
and emits no automatic category events or sounds.

## Saved data and verification

The snapshot and selected view share one versioned NVS record. A successful
write result follows durable saving; a failed write retains the previous record
and screen data. Observation time survives reboot. Unknown age is shown until
the clock synchronizes. Setup, pairing and error states keep the SDK status screen. During reconnection,
the saved report remains visible until Muse refreshes.

Hardware checks for this application remain pending; see [validation](VALIDATION.md).
Outdoor reports describe the selected place and observation period. The screen
is a family information display. Follow the provider's local notices and the
owner's established care preferences. Details: [setup](README.md).
