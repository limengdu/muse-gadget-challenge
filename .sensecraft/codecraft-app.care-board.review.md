# Care Board manifest review

Review date: 2026-11-05. Manifest: `codecraft-app.care-board.json`.
Source checkout: `project/care-board`, commit
`4dda0ae422e56046fe5bd97824c91c263d00c496`.
Scope: local authoring and review of an English CodeCraft Hub private draft.

## Evidence and decisions

| Item | Evidence | Decision / status |
|---|---|---|
| Name and purpose | `projects/care-board/README.md:17-21` | Preserve the established project name; family messages and reminders on an E1001 |
| Audience and language | Project guide and approved authoring brief | English; families and makers with the device and a Muse account |
| Device | `projects/care-board/README.md:25-44`, `esp32/devices/sdkconfig.reterminal-e1001` | One reTerminal E1001; overlay targets ESP32-S3 and enables Care Board, clock, events and e-paper cards |
| Scene | Approved scene ID and validation allowlist | String ID `7`; the live scene label was not queried in this offline review |
| Clone and commercial flags | Explicit project permissions | `allow_clone: 1`, `allow_commerce: 0`; platform settings are recorded separately from the source license |
| License | Root `LICENSE`; Apache-2.0 headers in all four files under `projects/care-board/`; `esp32/main/care_board.c` header | Apache-2.0 verified locally |
| Environment | Supplied preflight evidence | Global; org `424326273238784`; active SenseCAP Access Key reported; credential values are excluded from review artifacts |
| Account and author | Authenticated account identity was not supplied | `identity.account` remains null and `info.author` remains empty, as the manifest contract permits |
| Images | Approved asset selection | No images selected; cover and gallery are empty; a cover is required for publication |
| Activity | Reviewer reported successful Global `app activity list` with an empty list on 2026-11-05 | No activity; `activity_id: null`. Exact lookup time was not supplied; the result was not queried again offline |
| Public project source | Supplied project-specific GitHub URL; matching local project folder inspected | Included in the introduction; signed-out public accessibility remains to be checked online |
| Skill | `projects/care-board/SKILL.md:26-131`; project guide `README.md:65-72` | Full-file installation once after pairing; ask Muse to remember it; resend the full file after changes |
| Command implementation | `esp32/main/noise_control.cpp:1499-1525`, `esp32/main/care_board.c:158-240`, `esp32/main/care_model.c:178-330` | Registered update/state/clear commands, daily/once reminders, due/missed/ack transitions and key actions cross-checked |
| Event and notification limits | `projects/care-board/README.md:112-122,132-171`; `SKILL.md:93-131`; `esp32/main/gadget_events.c` | Muse chat delivery depends on Wi-Fi/session; phone alerts depend on account settings; event history clears on reboot |
| Hardware validation authority | `projects/care-board/VALIDATION.md:19-62` | Hardware claims use this record; implementation and host coverage are distinguished from device checks |

Public source:
https://github.com/limengdu/muse-gadget-challenge/tree/project/care-board/projects/care-board

Skill:
https://github.com/limengdu/muse-gadget-challenge/blob/project/care-board/projects/care-board/SKILL.md

## BOM evidence

Catalog verification was supplied by the reviewer from the Global API on
2026-11-05 using `app bom search`. The exact search filter, response body and
time of day were not supplied. This review carries that API confirmation as
provided evidence; it did not perform a fresh catalog lookup.

| Catalog SKU saved as `id` | Catalog name | Quantity | Catalog purchase URL | Role |
|---|---|---|---|---|
| `100073581` | reTerminal E1001 | `1` | https://www.seeedstudio.com/reTerminal-E1001-p-6534.html?___store=retailer | Runs Care Board, displays family messages and reminders, and provides the reminder buzzer and family response keys. |

Quantity and role are supported by the project hardware/control table and E1001
overlay. This is the only material row; the supplied catalog SKU, name and URL
are preserved exactly. Ordinary cables, computer and phone are prerequisites.
Custom rows: zero. Catalog-unverified rows: zero, relying on the supplied
reviewer confirmation. Remote BOM storage verification awaits a future upload.

## Deployment plan and supported tools

The manifest contains 32 flat blocks: two introduction paragraphs, six steps,
15 instruction paragraphs, eight bullets and one `flash-firmware` tool.

| Action | Prerequisites and inputs | Mechanism | Observable result | Readiness |
|---|---|---|---|---|
| Prepare device and account | E1001, USB data cable, desktop Chrome/Edge, Muse app/account, Bluetooth, 2.4 GHz Wi-Fi, personal token from gadgets.muse.ai | Reader preparation and linked backup guide | Hardware and personal setup inputs available | Documented |
| Flash Care Board | Exact clean release image in the ledger; USB connection and reader's own SDK token | `flash_care_board`, placed immediately after its instructions | Page completes firmware installation/provisioning; user follows Reset prompt | Firmware upload and three config values pending |
| Pair the device | Powered E1001, nearby phone and home Wi-Fi | Muse Settings > Devices > Developer mode > Add Device; select MuseGadget-Disp-…; Green confirmation | Muse app shows connected | Documented manual journey |
| Install skill and family settings | Paired board; full linked SKILL.md; names and local time zone | Paste the complete skill into Muse chat and ask Muse to remember it; configure through Muse | Intended names and local time appear | Documented manual journey |
| Try messages and reminders | Connected device and installed skill | Muse invokes care commands; parent presses Green | Note/reminder row, due buzzer, checked row/time and Muse reply | Main path has recorded hardware evidence; exact release installation pending |
| Operate and stop the plan | Correct local time; network/session; understanding of pending paths | Muse empties reminders or uses care.clear/care.update; documented setup reset | Plan stops or normal status page returns as requested | Implemented behavior documented; extended checks pending |

Flash configuration follows the complete Muse rows in the CLI tool-block
reference: SKU `100073581`, device type `58`, bin mode,
`muse_gadget: true`, `full_flash: false`, and the E1001 editor default
`flash_address: "0x90000"`. The supplied Muse image is linked for `0x20000`;
the Hub Muse flow supplies its segment layout. Locally,
`esp32/sdkconfig.defaults:74-81` and `esp32/partitions.csv:35` also establish
the standard partition table and `ota_0` at `0x20000`.
All tool settings live in the single nested `config` object. The reader supplies
their token through the page prompt. The page supplies the supporting segments.

## Artifact ledger

| Artifact | Local path | Existence / bytes | SHA-256 | Target / destination | Status |
|---|---|---|---|---|---|
| Muse application image | `/Users/mengdu/Desktop/mgc-release/care-board-4dda0ae/muse-gadget.bin` | Present; `1708032` bytes (`0x1a1000`) | `96a79d89def7e906c1520476daf27cb7e0489fbac288b28673a8fb979f2b15be` | E1001 / ESP32-S3; `flash_care_board.config.firmware_url` | Local bytes checked; upload pending |
| Cover / gallery | None selected | Pending photos | Pending | `images`, cover first | Publication input pending |
| Skill | `projects/care-board/SKILL.md` | Present | Source tracked at the checkout revision | Public GitHub link in step four; pasted into Muse by the reader | Source reference, not an uploaded binary |

Firmware provenance: the release brief records an empty-token rebuild linked at
`0x20000`. The release directory suffix matches the checkout's short revision.
Local inspection found ESP image magic `0xe9` and chip ID `9` (ESP32-S3).
The firmware length matches the app-size line in the validation record.
This authoring review did not rebuild or independently inspect a linker map.

The exact ledger image passed the quiet complete-token scan:

```sh
grep -aqE 'mgst_[A-Za-z0-9_-]{43}' /Users/mengdu/Desktop/mgc-release/care-board-4dda0ae/muse-gadget.bin
```

Exit code: **1**, PASS (no complete token pattern found). Only the result is
reported. Hashing and scanning were read-only. Before the future upload, rehash
and rescan the selected bytes, then populate the three fields from the upload
response: `firmware_url`, `firmware_name` and string `firmware_size`.
All three remain empty strings in this draft. Artifact paths and hashes are
kept in this review sidecar.

## Hardware evidence and pending checks

`VALIDATION.md` records one E1001 with CH340 console and 32 MB flash, paired on
home Wi-Fi, tested on 2026-11-04 at `29b7eb7` with ESP-IDF v6.0.1:

- Clean shared I2C startup and an SHT4x reading.
- Muse reads state and displays a message plus a short test reminder.
- Reminder due page, buzzer and `due` event in the family's Muse chat.
- Green confirmation, checked row/time and immediate `ack` event.
- Muse confirmation reply with the skill installed, and device reply capture.
- Silent `due` turn releases the queue so the next `ack` can proceed.
- Missed-reminder notification is separately attributed to `4623eb6`.

The same record reports 20 passing host tests and an app of `0x1a1000` bytes in
the unchanged `0x200000` slot, 19% free. These are recorded prior results,
not checks executed during manifest authoring. The current checkout/release
revision `4dda0ae` is distinguished from the recorded device-test revisions.
Hardware revision is not recorded in `VALIDATION.md`; unit count is one.

Pending device checks remain explicit in the page: `read`, `checkin` and
`call_request` delivery with the installed skill; overnight operation; daily
reset; Wi-Fi reconnection with queued events; and installation/provisioning of
the exact release through the Hub flash page. RTC backup supply and clock
recovery also remain household setup checks in the project guide. The page
describes a confirmation as a recorded key press, with direct contact for
health certainty and urgent concerns.

## Content and format review

Editorial review: **PASS for the requested local draft**.

- Name preserved; excerpt states the capability, audience and outcome within
  field limits. Introduction adds context and the public source/validation links.
- Prerequisites precede flashing; pairing precedes the one-time skill install;
  skill install precedes event-dependent use. Prompt examples include observable
  board and chat outcomes. Step titles contain actions; the UI supplies numbers.
- Daily and one-time scheduling, character/row limits, network dependency,
  phone notifications, power-loss event history and plan clearing are explained.
- Hardware claims are scoped to the validation record. Extra controls are
  labeled implemented with hardware delivery checks pending.
- Link destinations and GitHub heading anchors were checked against local
  files. Public accessibility and external destinations await online checking.
- Prose was authored for the Hub journey and compiled locally using
  `sensecraft-cli app doc compile --from-md`; compiler `warnings: []` and
  `pending_uploads: []` were inspected. The single tool was then inserted at the
  flash step in the final manifest. Text content is compiled HTML/plain text.
- Local `sensecraft-cli app fields` and `sensecraft-cli app blocks` both exited
  zero and confirmed the field structure and tool configuration.
- Final local integrity audit passed: all six linked GitHub paths and applicable
  heading anchors exist in the checkout; name/excerpt use 31/199 UTF-16 units;
  the firmware SHA-256 remained unchanged; both deliverables have no complete
  SDK-token pattern. `git diff --check` exited zero. Working-tree inspection
  showed exactly the two new deliverables.

Validation command:

```sh
python3 /opt/homebrew/lib/node_modules/@seeed-studio/sensecraft-cli/skills/sensecraft-cli/create-app/assets/validate_manifest.py codecraft-app.care-board.json --scene-ids 1,2,3,4,5,6,7,8,999
```

Result: **PASS**, exit code **0**, **3 expected warnings**:

1. No images; private drafting is supported and a cover is needed for publishing.
2. `deployment.blocks[12].config.firmware_url` is pending.
3. The same firmware URL is empty, pending a deployment input.

The two firmware warnings describe one unresolved URL. Shape validation is
separate from API catalog evidence, consumer compatibility and device testing.

## Handoff and readiness

| Finding | Result |
|---|---|
| Format validated | PASS with the three expected warnings above |
| BOM catalog identity | Reviewer API-confirmed on 2026-11-05; preserved exactly |
| Editorially reviewed | PASS for an English local private draft |
| Deployment inputs complete | Pending uploaded firmware URL/name/size |
| Publishing inputs complete | Pending cover and the remaining release/device checks |
| Remote app / sync | No app created; app ID and created time null; revision 1, last pushed revision 0, timestamps/hash null |

Next inputs are the firmware upload response and cover photo. The existing clean
local binary is the upload candidate; the reader's personal token is supplied
at deployment time. Optional account/author attribution remains unset until an
authenticated identity is established. Verify public source access signed out,
test the uploaded image through the Hub draft on E1001, and record the remaining
device results before publication. Future payload projection omits image keys
while the image list is empty and carries only server fields from the manifest.

This session performed local reading, compilation, hashing, scanning and
validation only. Cloud commands, firmware builds, serial operations, flashing,
and commits were not performed. The two deliverables remain uncommitted.
