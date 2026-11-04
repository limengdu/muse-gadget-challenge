---
name: gadget-fridge-notes
description: >-
  Manage the family's shopping, chores, to-do and notes lists on a Seeed reTerminal
  Sticky running Fridge Notes. Turn Sticky voice notes into list items, assign
  people, track completion and read the VM mirror while the device sleeps.
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


# Fridge Notes on reTerminal Sticky

The Sticky is the household's shared fridge list: an experimental Muse board with
a 480×800 portrait e-paper screen, touch, an AI voice-note button and a microphone.
Replies are text on the screen. The board has no speaker. The device stores lists
in NVS and mirrors them to `gadget/fridge-notes/lists.json`, relative to the user's
home on this Muse VM. Anyone at the fridge can read or tick an item.

## Find the lists

Use fresh device discovery and its registered `notes.*` commands to identify the
Fridge Notes gadget. Read `notes.get` before referring to an existing item's ID.
Use the family's existing list slugs and titles. Suggested defaults are:

| Slug | Default title | Put here |
|---|---|---|
| `shopping` | Shopping | Groceries, quantities and household supplies |
| `chores` | Chores | Household work, optionally assigned to a person |
| `todo` | To-do | Other actionable reminders |
| `notes` | Notes | Short information for people at home |

Create only the lists the user needs, with `notes.set` and an empty `items` array.
Titles may use the family's language; slugs are lowercase ASCII letters, digits
or hyphens, 1–23 bytes. There are at most four lists and twenty items in each.
Titles and `who` take 1–31 UTF-8 bytes, and item `text` 1–127 UTF-8 bytes, single
line. A Chinese character normally takes three bytes. Keep wording concise while
preserving quantities and ownership. Ask the user to shorten text when its
meaning cannot fit the limits.

## Commands

Invoke these through the selected gadget's registered commands, using JSON
parameters. A successful command returns `ok: true` and a `payload` with
`version: 1`, a monotonic `revision`, `next_id` and a `lists` array. Each list
contains `list`, `title` and `items`; each item contains numeric `id`, `text`,
boolean `done`, optional `who` and UTC seconds `added_at` (0 means time was unknown).
Writes return the affected list. Reads preserve completed items too.

| Command | Parameters | Use |
|---|---|---|
| `notes.get` | `{list?}` | Read all lists, or one slug |
| `notes.add` | `{list, items: [text or {text, who?}]}` | Append 1–20 items; creates a missing list |
| `notes.set` | `{list, title?, items: [...]}` | Replace a list with 0–20 items |
| `notes.toggle` | `{list, id, done?}` | Set a checkbox, or invert it when `done` is omitted |
| `notes.remove` | `{list, id}` | Remove one item |
| `notes.clear_done` | `{list}` | Remove all completed rows in that list |

`notes.set` accepts strings or `{text, who?, id?, done?, added_at?}`. Omitted
IDs and timestamps are generated; supplied IDs must be unique across all lists,
positive integers below 2147483647. Supplied timestamps range from 0 to 4102444799.
Use replacement for an expressly requested reset or a reviewed migration. Use
`notes.add` for additions. `title` is a parameter of `notes.set`.

Examples:

```json
{"list":"shopping","items":["Eggs","Soy sauce","Milk, 2 cartons"]}
```

```json
{"list":"chores","items":[{"text":"Saturday cleanup","who":"Leo"}]}
```

```json
{"list":"shopping","id":12,"done":true}
```

Use `notes.get` and a fresh ID before marking or removing an item. Prefer an
explicit `done` value for a completion request. After an uncertain append or
replacement response, read the list before retrying so the user's items stay
unique. Queue-busy and storage-failed responses mean the edit was not accepted;
retry after checking availability. All items in one write are validated together.

## Turn a voice note into items

A Sticky AI-button note uses Muse's existing voice-note conversation. When the
user speaks a list request from this gadget:

1. Identify the intended list, using Shopping for groceries and Chores for
   household jobs. Use a named list when the user names one; resolve ambiguity
   briefly when it changes the outcome.
2. Split independent requests into separate items: “eggs are out, and get soy
   sauce” becomes “Eggs” and “Soy sauce”. Keep phrases such as “mac and cheese”
   together. Preserve negation and corrections as edits to the intended item.
3. Keep quantities, units and useful time cues in `text`, for example “Milk,
   2 cartons” or “Saturday cleanup”. “For Leo” on a chore maps to `who: "Leo"`.
   A due-day phrase remains text; the firmware's `added_at` records creation time.
4. Read the existing list for duplicates or corrections. Call `notes.add` with
   the resulting items. Existing items can be completed or removed by ID when
   the request calls for it. If full, explain which list is full and offer the
   requested cleanup or a shorter batch.
5. Confirm only after the command succeeds, in one short sentence such as
   “Added eggs and soy sauce to Shopping.” Keep the device's reply short enough
   to read on e-paper. General conversation follows normal Muse behaviour.

For “what's left?”, read the appropriate list and report only `done: false`
items, retaining quantities and `who`. Summarize by person for an ownership
question, and say when a list has no incomplete items.

## Read while Sticky sleeps

When device commands are unavailable, read `gadget/fridge-notes/lists.json` on
this VM. Expect the same version-1 payload schema, containing every list.
Treat the file as the latest mirrored snapshot and say that it may lag behind
changes made while offline. A missing file means no mirror is available yet.
An incomplete JSON document can be a multi-chunk upload: retry reading after a
short delay and report unavailable data if it remains incomplete.

The device owns this mirror. Send edits through `notes.*` once it is reachable;
explain when an edit is waiting for the device rather than announcing it as
saved. The firmware uploads about three seconds after edits settle and retries
an offline or interrupted write after thirty seconds. A full upload starts by
overwriting the file, then appends bounded UTF-8 chunks. The on-device sync label
updates after the entire upload succeeds.

## React to device events

These are messages in the user's own chat, sent only for touch changes. A voice
command already has its command result and confirmation.

| Chat prefix and summary | Reaction |
|---|---|
| `[fridge-notes event] Ticked "Eggs" on the Shopping list.` | Briefly acknowledge completion if useful; the change is already saved |
| `[fridge-notes event] Unticked "Eggs" on the Shopping list.` | Acknowledge the item is back on the list |
| `[fridge-notes event] Cleared 3 done items from Shopping.` | Confirm the completed rows were cleared |
| `[fridge-notes event] Updated the fridge lists with 4 touch changes.` | Treat as one grouped update; use `notes.get` for details when needed |

Relay these short natural summaries as-is or in the user's language. Read the
current state when an event needs more context; its change has already happened.
`events.poll` can recover recent events after missed chat messages. Its history
is bounded and RAM-only, while the lists persist across reboots.

## Useful routines

When the user requests a routine, agree its time, timezone and action, then use
Muse's normal routine facility. Examples include:

- A weekly Sunday review: read incomplete Chores and To-do items, group by
  person, ask whether old tasks should stay, and clear completed rows when that
  cleanup is part of the agreed routine.
- Before the normal grocery trip: read the remaining Shopping items with their
  quantities; use the VM snapshot if the Sticky is asleep.
- A weekly household check-in: surface unassigned chores and ask who will do
  them, then apply reviewed changes through the live gadget commands.

Use `clock.set` with the actual current UTC epoch and local UTC offset in
minutes (-840 through 840) for the sync label. Update the offset when daylight
saving changes. Hardware button recording, pairing confirmation, Wi-Fi text
entry and battery sleep retain the board's existing behaviour.

## Limits and privacy

This board and application are experimental. Hardware verification is recorded
in the [project verification notes](VALIDATION.md). The display retains its
last image during sleep and power loss; anyone nearby can see it. Item text and
names reach the user's Muse VM and chat through their existing encrypted Link
session. Use ordinary household notes and explain shared visibility when asked.
Device-side storage is local NVS; deletion uses the commands above. The firmware
provides list tracking and creation timestamps; calendar alarms, outbound family
messaging and automatic physical actions use separately agreed Muse routines.
