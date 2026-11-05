# Outdoor Air: Hub manifest review

Review date: 2026-11-05. Manifest: `codecraft-app.outdoor-air.json`.
Worktree: `/Users/mengdu/Desktop/mgc-indicator`; branch: `project/air-guardian`.
Inspected source HEAD: `81cdb800cc2af9a214b6aab97f5259c1218b5986`.
The initial working tree was clean. This review covers a local, unsubmitted manifest.

## Method and references

Applied the installed [create-app skill](/opt/homebrew/lib/node_modules/@seeed-studio/sensecraft-cli/skills/sensecraft-cli/create-app/SKILL.md).
The following references were read in full before authoring, relative to
`/opt/homebrew/lib/node_modules/@seeed-studio/sensecraft-cli/skills/sensecraft-cli/`:

- `SKILL.md`
- `create-app/SKILL.md`
- `create-app/references/project-discovery.md`
- `create-app/references/content-review.md`
- `create-app/references/manifest.md`
- `create-app/assets/manifest-template.json`
- `references/app-fields.md`
- `references/tool-blocks.md`, including the Indicator and Muse flash rules
- `references/app-commands.md`, including `app doc compile`
- `references/files.md`

Repository guidance inspected: root `AGENTS.md`, `challenge/AGENTS.md` and
`challenge/README.md`. Execution used local file inspection, `app fields`,
`app blocks`, `app doc compile` and format validation. Cloud calls, uploads,
builds, serial access and commits were outside this run. Existing release-build
configuration and outputs were left intact.

## Evidence and decisions

| Item | Evidence | Decision or finding |
| --- | --- | --- |
| Name, audience and language | Project README title; owner-established audience and language | Outdoor Air; English; families and makers with a D1 and Muse account |
| Data journey | `projects/air-guardian/README.md`, Hardware and data / Muse setup; `SKILL.md`, Refresh the home glance | Muse looks up a chosen place and pushes an outdoor snapshot; lookups, preferences and schedules run in Muse |
| Implemented command surface | `esp32/main/noise_control.cpp`, Outdoor Air registration; `esp32/main/air_guardian_core.c`, command handler | `air.update`, `air.status`, `air.set_view`; shared `clock.set` supplies clock/offset |
| Target and isolation | `esp32/devices/sdkconfig.sensecap-indicator`; `esp32/main/Kconfig.projbuild`; `esp32/main/app.c`, sensor-reader/registration guards | ESP32-S3 Indicator D1 only; app enabled; indoor sensor dependency skipped by app logic |
| Views, saved state and age | `air_guardian_core.c`, parse/save/restore and age; `air_guardian_render.c`, screen rendering; project README | Ambient, numbers and night implemented; snapshot/view saving implemented; refresh indicated at three hours or unknown age |
| Pairing | Root `README.md`, Step 3; Indicator overlay BLE suffix | Muse Settings > Devices, Developer mode, Add Device, matching MuseGadget name, top-button confirmation, 2.4 GHz Wi-Fi |
| Skill installation | Owner-established deployment journey; complete project `SKILL.md` | Paste full linked skill into Muse chat once and ask Muse to remember it; repeat after skill changes |
| Scene | Owner-established scene ID and supplied valid-ID set | String `2`; live scene labels were not queried |
| Permissions | Owner-established choices | `allow_clone: 1`; `allow_commerce: 0`, independently of the source license |
| License | Apache-2.0 headers in all four project Markdown files and `air_guardian*` source files | `Apache-2.0` |
| Environment | Owner-established Global profile, org `424326273238784`, active SenseCAP Access Key | Recorded site/org; account identity unknown, so `identity.account` is null and `info.author` is empty |
| Activity | Reviewer reported successful Global `app activity list` with an empty list on 2026-11-05 | No available activity; `activity_id: null`; no activity selection or association submitted |
| Images | Owner-established private-draft choice | Empty image list; cover required for publishing |
| Remote identity and sync | Fresh local manifest; no remote write in this run | Null app ID/creation time; local revision 1; last pushed revision 0; no remote baseline |

Public project source:
https://github.com/limengdu/muse-gadget-challenge/tree/project/air-guardian/projects/air-guardian

Public skill:
https://github.com/limengdu/muse-gadget-challenge/blob/project/air-guardian/projects/air-guardian/SKILL.md

Both required links are present in the page. The validation link targets the
same branch and project folder. URL reachability and signed-out public access
remain to be checked when network access is available.

## Hardware evidence boundaries

`projects/air-guardian/VALIDATION.md`, Final revision on the device (one D1),
records owner results on one unit, board revision unspecified:

- Fresh-boot pairing succeeded.
- `air.status`, `clock.set` and `air.update` answered, with a snapshot shown.
- No panel artefacts were observed with the final event/strip revision and
  20-line RGB bounce buffers. Current source has `LCD_BOUNCE_LINES 20`.
- Two redraws were observed in three minutes: display at boot and age after
  clock synchronization.

These later recorded results supply the pairing/display claims. The skill's
general hardware-pending sentence and earlier pending-retest paragraphs are
read together with this specific final-revision record. The manifest preserves
the remaining hardware limits: complete view/button interactions, night
readability, reboot persistence, reconnect/setup/error paths, stale-age
transitions, image return and complete Muse routines. It also identifies the
uploaded-image Hub draft test as pending.

The validation record reports ESP-IDF v6.0.1 helper-build success with app
`0x1b1000` in a `0x200000` slot, 15% free; final combined host checks report
20 passes and renderer checks 14 passes. Its full-suite record has the same
baseline 1 failure, 8 errors and 5 skips, and fresh-build validation is pending
because of the documented component-manager sandbox boundary. These are
existing records, rather than checks rerun during manifest authoring. The
record cites base `7dd55c2` and foundation `b227353`; it does not establish an
exact tested source hash for the future release binary. Bind that binary to
its release revision when it becomes available.

## BOM evidence

Status: **API-confirmed by the reviewer on 2026-11-05**, reused from the
established review result. Command family: `sensecraft-cli app bom search`.
The exact search argument and raw response were not supplied; the confirmed
selected row was supplied directly. No additional lookup was performed offline.

| SKU saved as id | Catalog name | Quantity | Catalog purchase URL |
| --- | --- | --- | --- |
| `114993068` | SenseCAP Indicator D1 | `1` | https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html?___store=retailer |

Role: receives Muse-sourced snapshots and runs the Outdoor Air LCD dashboard.
The row explicitly limits compatibility to D1, excluding D1S, D1L and D1Pro.
Quantity one is supported by the project and the established BOM. Ordinary
USB cables and desktop/phone prerequisites are listed in setup instructions.
The manifest contains one catalog row; saved-remote-BOM verification awaits
creation. Uniform type `59` is a tool setting separate from the SKU.

## Deployment actions and tools

| Order | Action and prerequisite | Tool or supported path | Observable result / readiness |
| --- | --- | --- | --- |
| 1 | Prepare D1, Muse account/app, personal SDK token, USB data cable, Chrome/Edge and 2.4 GHz Wi-Fi | Prerequisite instructions and token-account link | Required personal inputs available at deployment time |
| 2 | Install Outdoor Air on the connected D1 | `ff_outdoor_air_d1`, `flash-firmware`, immediately after its instructions | Reviewed release file exists; upload pending; page supplies reset/token prompts |
| 3 | Pair nearby device and provide Wi-Fi | Muse app Developer mode / Add Device / top-button confirmation | Muse shows the device connected |
| 4 | Install skill once and choose place/preferences | Linked full `SKILL.md` pasted into Muse chat and remembered | Muse receives command/data instructions and household context |
| 5 | Ask for an outdoor update, report age, views or an optional routine | Muse chat and advertised gadget commands | Snapshot shown; observation age readable; other interactions have stated hardware limits |
| 6 | Read data and validation limits | Linked validation record and reader-facing limitations | Source age and pending hardware coverage are explicit |

There are 25 flat blocks: 2 introduction paragraphs, 6 steps, 16 additional
text blocks and 1 firmware tool. The tool has one nested `config` object:
SKU `114993068`, uniform type `59`, bin mode, default address `0x0`,
`muse_gadget: true`, `full_flash: false`. The owner's release contract requires
`muse-gadget.bin` linked at `0x20000`. The Muse page supplies the segmented
layout and per-user token prompt. The default editor address and application
link address serve separate purposes.

## Artifact ledger

| Artifact | Local path / selection | Existence, bytes and SHA-256 | Destination |
| --- | --- | --- | --- |
| D1 release firmware | `/Users/mengdu/Desktop/mgc-release/outdoor-air-81cdb80/muse-gadget.bin` | Exists; 1773568 bytes; SHA-256 `c60cd81e205f076bfdbc11cc6022174081d5dd2bbe582fa0f522a1f48d722b22` | `deployment.blocks` tool ID `ff_outdoor_air_d1`, `config.firmware_url`, `config.firmware_name`, `config.firmware_size` |
| Cover/gallery | None selected | No image artifacts | `images`, cover at index 0 when selected |

Firmware target: SenseCAP Indicator D1 / ESP32-S3, application linked at
`0x20000`, per the owner's release specification. The owner reports an
empty-SDK-token rebuild. The release directory revision `81cdb80` matches the
inspected source HEAD prefix. Local inspection verified the file exists,
has ESP image magic `0xe9` and header chip ID 9, and remained unchanged in
size/modification time while its bytes were read and scanned. The exact
selected file's quiet complete-SDK-token scan **passed, exit 1**. The size
and SHA-256 above were computed from those bytes. The application link
address remains established by the owner's release specification rather
than an independent linker-map check in this run. Upload bookkeeping is
reserved for the reviewer; all three firmware fields remain empty strings,
including string-valued size.

Reviewed evidence-file digests (local bytes):

| Path | Bytes | SHA-256 |
| --- | --- | --- |
| `projects/air-guardian/README.md` | 6530 | `7bfaaba5db38382aed6060a72b0b9539abb4cba7be4a903732c5d390cc83ecc2` |
| `projects/air-guardian/README.zh-CN.md` | 6152 | `c2b867e024fa1c82cf8c5d2489a32149cf9ae3571419ee79ba5022f88d887906` |
| `projects/air-guardian/SKILL.md` | 6315 | `c8af79f9a5cf22dcdffa4842d5723726421009647ad1b241c3fe880d770c274f` |
| `projects/air-guardian/VALIDATION.md` | 16944 | `59b232ffcc1cb239e32d5681d0e350d366bc4ebbee8779c1fa16d3343c8098ef` |

## Pending inputs and follow-through

1. Preserve the reviewed release file and its ledger. Before upload, confirm
   the selected bytes still match the recorded size and SHA-256, the D1
   target and `0x20000` link specification apply, and its quiet token scan
   still passes. A readable file with no match passes that scan.
2. The reviewer uploads the reviewed firmware as type 4 and fills
   `ff_outdoor_air_d1.config.firmware_url`, `firmware_name` and string
   `firmware_size`. Update the flash instruction's readiness sentence when
   the file is available, then revalidate the manifest.
3. Select a cover before publishing. Private-draft authoring can retain the
   current empty image list. At future payload projection, zero images means
   omitting both `images` and `pic_url` from the payload.
4. Verify the public source/skill links while signed out and test the exact
   uploaded firmware through the Hub draft, recording its release revision,
   app ID, board revision/unit count and the remaining hardware flows above.
5. Account identity/author metadata can be filled from an established account
   identity at a later authenticated review. Their current empty/null values
   are permitted. Future cloud work checks the existing Global/org profile
   and current activity availability before submitting.

## Editorial and validation result

Editorial review: **PASS for a local private-draft manifest**. The name is
preserved; excerpt is 142 UTF-16 code units; introduction adds data-flow/time
context and the project source link. Prerequisites precede the flash tool;
pairing uses the documented top button and 2.4 GHz Wi-Fi; skill installation
uses the full contents and includes the update path. Natural requests have
observable outcomes. Implemented functionality is distinguished from the
limited owner-tested hardware results. The pending firmware is visible at
its point of use. Prose was compiled to HTML/text before tool insertion;
step titles contain action names and the tool stays beside its instructions.

Local commands completed successfully:

```sh
sensecraft-cli app fields
sensecraft-cli app blocks
sensecraft-cli app doc compile --from-md /private/tmp/outdoor-air-hub-81cdb800.md
python3 /opt/homebrew/lib/node_modules/@seeed-studio/sensecraft-cli/skills/sensecraft-cli/create-app/assets/validate_manifest.py codecraft-app.outdoor-air.json --scene-ids 1,2,3,4,5,6,7,8,999
git diff --check
```

Exact selected firmware scan:

```sh
grep -aqE 'mgst_[A-Za-z0-9_-]{43}' /Users/mengdu/Desktop/mgc-release/outdoor-air-81cdb80/muse-gadget.bin
```

Result: exit 1, no complete SDK-token pattern found. The scan was quiet and
only its status was reported.

Compilation: zero warnings and zero pending media uploads. Its pending-media
list covers compiled prose assets; the firmware tool was inserted afterward.
Manifest validation: **PASS, exit 0, zero violations, three expected warnings**:

- No images; a cover is required before publishing.
- `deployment.blocks[10].config.firmware_url` is pending.
- The same `firmware_url` is empty (second warning for that single pending input).

Readiness: **format validated; BOM reviewer-API-confirmed; editorially
reviewed; deployment inputs incomplete; publishing inputs incomplete**.
This is an authored local draft with a pending firmware URL, rather than a created
Hub app. Remote storage, catalog association, uploaded-image deployment and
public-link visibility have no verification result from this offline run.
