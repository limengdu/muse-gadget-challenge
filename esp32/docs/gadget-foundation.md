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

# Gadget foundation

The E1001 and E1002 overlays enable `HOMEHUB_GADGET_EVENTS`,
`HOMEHUB_GADGET_CLOCK` and `HOMEHUB_EPAPER_CARDS`. Other projects select
these options individually. Firmware builds use ESP-IDF v6.0.1.

## Device events

Call `gadget_events_post(source, summary, data)` from a task after app startup.
It copies the arguments, attempts to queue immediately and returns a boolean.
Sources hold up to 47 bytes, summaries 511 bytes and compact JSON data 1024 bytes.
The message is `[source event] summary`, followed by a newline and compact JSON
when data is supplied. A failed allocation, invalid input or busy queue lock
returns false. Producers can retry after a false return.

Receive rows and reply candidates are allocated once from PSRAM when available;
boards without PSRAM allocate them in internal RAM. A PSRAM allocation failure
leaves event initialization unsuccessful.

One worker sends events in order over Link. It subscribes to `/chat/subscribe`
before posting JSON to `/chat/stream`, correlates replies with the POST's
message ID and retains at most 600 UTF-8 bytes of the most recent reply.
`gadget_events_reply()` copies that reply. Unsent requests retry with a
1–30 second exponential delay. A submitted message is drained for up to 90
seconds; an unavailable reply is reported while the next event proceeds.

The pending queue holds 16 events in addition to the current turn. When full,
it evicts its oldest pending entry and increments `dropped`. A separate ring
retains the last 32 accepted events, including entries evicted from delivery.
`events.poll` accepts an optional nonnegative integer `since` and returns
`events`, `last_seq` and `dropped`. Each entry contains `seq`, `time`,
`time_basis` (`epoch` or `uptime`), `source`, `summary` and optional `data`.
Sequence numbers are monotonic within one boot; the queue and ring live in RAM.

On the Link console, `>event=Ready for school` posts a `console` event.
Progress is emitted as `@event` JSON with `queued`, `sent`, `reply` or `error`.
The `reply` record includes `text`. Reply content is written through this
console path. `sdk_token_console_register_handler()` registers additional
startup line handlers; the SDK token handler runs first.

## Local time

The Wi-Fi IP callback starts `esp_netif_sntp` with `pool.ntp.org`. UTC becomes
valid from 2024 through 2099. `gadget_clock_set(utc_epoch, utc_offset_min)` also
sets UTC and persists the offset with `config_store` under `utc_offset_min`.
Accepted offsets range from -840 to 840 minutes. `clock.set` exposes these
two integer parameters to Muse and writes NVS on its own worker.

Use `gadget_clock_valid()`, `gadget_clock_epoch()`,
`gadget_clock_local(&tm)` and `gadget_clock_minutes_of_day()` for page content.
The last helper returns -1 while UTC is invalid. Local time is UTC plus the
stored fixed offset; projects can update the offset for daylight saving changes.

`gadget_clock_set_change_cb()` registers a quick notification after SNTP or manual
clock changes. The callback can wake an application worker for RTC I/O.
`gadget_clock_restore_utc()` restores UTC while retaining the saved local offset;
`gadget_clock_offset_minutes()` reads that offset. Care Board uses these hooks for
its shared PCF8563 hardware clock.

## Card pages

`epaper_cards_canvas()` wraps panel bytes: 1-bit MSB-first black/white, or
4-bit high-nibble-first Spectra 6 ink codes 0, 1, 2, 3, 5 and 6. All non-white
colours become black on E1001. The API supplies rectangles with an optional
radius, horizontal/vertical lines, proportional text, measurement, wrapped
text with a final ellipsis, named icons, checkboxes and ink inversion.
Text coordinates refer to the top of a line; the font carries its baseline.
Supported text sizes are 28, 40, 60 and 96; icon sizes are 64 and 112.
`epaper_cards_text_box()` returns its line count and truncation status.

Publish a page with `epaper_cards_show(render, ctx)` and request updates with
`epaper_cards_request_redraw()`. Both attempt to queue display work immediately (false means retry); the existing e-paper
task invokes the callback and performs one full panel refresh. The callback
writes directly into the panel frame. Keep callback code and context alive
through all outstanding callback invocations, and synchronize shared page data.
Callbacks may use a static application model or copy a snapshot of that model.

A dirty flag merges redraw requests during a refresh into one follow-up render.
A page generation discards a render replaced while its callback was running.
The panel mutex serializes cards, images and status refreshes. Status changes
remain pending while a card or image is displayed. `display.draw_url`,
`display.draw_file` and `display.show_animation` replace the card through the
existing image/status paths. When cards are disabled, the existing status and
image implementation remains active.

## Assets and host previews

Generate assets from the repository root with the local Pillow environment:

```sh
.venv/bin/python esp32/tools/gen_card_assets.py
```

Text comes from `components/muse/eink/fonts/montserrat.ttf` (SIL OFL 1.1).
En/em dashes and curly quotes are assembled from its dash and comma outlines.
The icon inputs are `.cache/fonts/MaterialSymbolsRounded.ttf` and the adjacent
`.codepoints` file and `MATERIAL-LICENSE` at the repository root (Apache-2.0).
The generator chooses FILL=1, GRAD=0, optical size 48 and weight 500 by axis name.
Extend the sorted `ICONS` list to add a symbol. The generated C arrays are const
flash data; glyph/icon metrics retain bounding boxes and proportional advances.
`main/card_assets_manifest.json` records input hashes and ESP32 data sizes,
including metadata and names; generation enforces a 160 KiB data budget.
Full third-party licenses are in `main/card_assets_licenses/`.

From `esp32/`:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
CARD_PREVIEW_DIR=/tmp/card-preview python3 -m unittest tests.test_epaper_cards
```

The second command writes `e1001_sample.pgm` and `e1002_sample.ppm` at 800×480.
Host harnesses exercise event delivery, clock offsets, ink packing, layout,
clipping and page replacement. Panel timing, physical ink appearance and live
Muse delivery are verified on the devices.
