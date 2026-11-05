# Door Sign Hub manifest review

Review date: 2026-11-05. Canonical manifest: [`../codecraft-app.door-sign.json`](../codecraft-app.door-sign.json). Local revision: 1. Status: reviewed local draft with pending firmware inputs and cover.

## Scope and identity

- Worktree: `/Users/mengdu/Desktop/mgc-xiao`; branch: `project/door-sign`; inspected source HEAD: `0a1381a56fc8f51f80554023091df804ad66cc15`. The worktree was clean at discovery.
- Global site and organization `424326273238784`, with an active SenseCAP Access Key, are owner-established environment facts. Authentication was not queried during this offline task. Key values are absent from the deliverables.
- This is a new local manifest. App ID, creation time and account remain null; author is an empty string because an authenticated account identity was not supplied. Remote creation and synchronization have not occurred; the last-pushed revision remains 0 with no baseline.
- This session authored the two requested files and used local schema, compilation and validation commands. Firmware builds, hardware operations, serial access, uploads, cloud commands and commits were outside this session's execution scope.

## Required reference inspection

Read in full before authoring, under `/opt/homebrew/lib/node_modules/@seeed-studio/sensecraft-cli/skills/sensecraft-cli/`:

- `create-app/SKILL.md`
- `create-app/references/project-discovery.md`
- `create-app/references/content-review.md`
- `create-app/references/manifest.md`
- `create-app/assets/manifest-template.json`
- `SKILL.md`
- `references/app-fields.md`
- `references/tool-blocks.md`, including the Muse SKU, address, reset and token rules
- `references/app-commands.md`, including local `app doc compile`
- `references/files.md`

Local `sensecraft-cli app fields` and `sensecraft-cli app blocks` both exited 0. Their static catalog confirms the canonical field types, flat ordered blocks and nested firmware configuration; it is structural evidence rather than a live product lookup.

## Project evidence and decisions

| Subject | Evidence | Applied decision |
|---|---|---|
| Project name, audience and purpose | `projects/door-sign/README.md:17-24`; owner-set audience | Preserve Door Sign; write English for families and makers with this device and a Muse account. |
| Display states and visitor interactions | `projects/door-sign/README.md:29-45,93-105`; `esp32/main/door_sign.c:133-185,281-297`; `esp32/components/muse/muse_door_sign.c:35-59,116-152` | Describe availability, up to three touch replies, queue confirmation and visitor voice notes; distinguish queue acceptance from delivery. |
| Target hardware | README hardware section; `esp32/devices/sdkconfig.muse-seeed-xiao-s3-147`; `esp32/components/muse/boards/board_seeed_xiao_s3_147.c:53-54,363-376`; owner-provided platform mapping | ESP32-S3 XIAO 1.47-inch Touch Display, 172x320; SKU `100069905`, uniform type `57`. nRF52840 is unsupported. |
| Feature enablement | XIAO overlay has `CONFIG_MUSE_DOOR_SIGN=y`; `esp32/components/muse/Kconfig:143-155`; `esp32/main/app.c:1879-1881`; `esp32/main/noise_control.cpp:1502` | App uses the full Muse UI with door-sign commands and clock/events. |
| Pairing | `projects/door-sign/README.md:73-76`; `esp32/AGENTS.md`, First boot and pairing | Muse Developer mode, Add Device, matching nearby MuseGadget name, USR1 confirmation, 2.4 GHz Wi-Fi. This is the documented setup path; final release onboarding awaits a Hub-page test. |
| Skill installation and time | README pairing section; `projects/door-sign/SKILL.md:32-41,69-85`; owner-set deployment journey | Paste the full linked SKILL.md into Muse chat once and ask Muse to remember it. Repeat after skill changes. Establish the owner and local time zone before timed signs. |
| Example requests | README Things to say to Muse; SKILL Turn requests into commands | Explain the expected sign for meeting, focus, free and custom requests, and clearing back to the face. These examples describe implemented behavior rather than claiming every example was separately checked on hardware. |
| Optional routines | `projects/door-sign/SKILL.md:121-135` | Calendar and recurrence depend on Muse's connected services. The page states that these flows await project hardware verification. |
| Deadline, queue and reboot limits | README Events, limits and privacy; SKILL Limits and privacy; `esp32/main/door_sign.c:78-85,167-185,205-225`; `esp32/main/door_sign_model.c:236-252` | One sign and deadline; next-occurrence local time; bounded in-memory event delivery; designed persistence is distinguished from pending power-cycle hardware checks. |
| License | Apache-2.0 headers in all four project files and `esp32/main/door_sign.c` | Record Apache-2.0 independently of the platform permission choices. |
| Scene | Owner's settled scene ID and supplied allowed ID set | Store string `2`; a human-readable catalog label was not supplied and is not guessed. |
| Permissions | Explicit owner decision | Cloning enabled (`1`), commercial use disabled (`0`). |
| Images | Explicit owner decision | Empty image selection for the private draft; publishing requires a cover. Existing simulator captures are project evidence rather than selected app images. |

Hardware evidence comes from the dated `VALIDATION.md` record. Its hardware table is more specific than the README's general pending-check sentence.

## Activity evidence

The reviewer ran `sensecraft-cli app activity list` on the Global site on 2026-11-05 and reported a successful empty list. The established selection is no activity, represented by `info.activity_id: null`. There is no candidate, ID, activity window or guide to evaluate. The exact lookup time and raw response were not supplied; this is reviewer-provided API evidence, not a new lookup by this session. No participation or submission has occurred.

## BOM evidence

The reviewer API-confirmed the following exact row with `app bom search` on 2026-11-05. The selected row was supplied in the task; the precise search term, raw response and time of day were not supplied. No cloud or sandbox catalog query was run here.

| Saved SKU (`id`) | Exact catalog name | Quantity | Purchase URL | App role and variant |
|---|---|---|---|---|
| `100069905` | XIAO 1.47" Touch Display (ESP32-S3) | `1` | https://www.seeedstudio.com/1-47-Inch-Touch-Display-Powered-by-XIAO-ESP32-S3-Plus-p-6996.html?___store=retailer | Door status display, touch replies and visitor microphone. ESP32-S3 variant required; the nRF52840 variant is not supported. |

Quantity and role follow the project's single-display hardware requirement. Ordinary USB cables, power and the computer are prerequisites. There are no custom or unverified BOM rows. API identity confirmation is inherited from the reviewer; saved remote BOM verification awaits app creation.

## Deployment plan and block review

The manifest contains 34 flat blocks: 2 introduction paragraphs, 7 step headings, 24 instruction/list blocks and 1 `flash-firmware` tool. Each step heading names an action without a manual step number. The firmware tool is immediately after its flash instructions, before the pairing step.

| Action | Prerequisites and evidence | Supported path / observable result | Readiness |
|---|---|---|---|
| Prepare device and accounts | Matching board, Muse app/account, SDK token, USB data cable, Chrome/Edge, Wi-Fi and power; README setup and owner-established journey | Instruction blocks; reader has the listed prerequisites and backup reference. | Copy complete; personal inputs supplied at deployment. |
| Flash Door Sign | Matching USB device and released image; Muse rules in tool-blocks.md | One `flash-firmware` block; browser device selection, reset and personal SDK-token prompt; Muse setup screen after restart. | Firmware upload metadata pending. |
| Pair the display | Flashed device, Muse app and 2.4 GHz Wi-Fi; README pairing section | Instruction blocks; Add Device, matching MuseGadget name, USR1 confirmation, normal Muse screen. | Documented path; final release Hub onboarding test pending. |
| Install skill once | Paired gadget, full public SKILL.md and owner's time zone | Instruction blocks with direct skill link and explicit remember/use request; Muse acknowledges the skill and owner context. | Copy complete; user performs in Muse chat. |
| Set a sign | Installed skill, connection and valid time for deadlines | Chat request examples with expected display state; optional routines state service dependencies. | Core sign setting and expiry have recorded hardware results; final release and optional routines await verification. |
| Send visitor messages | Active sign and connection; README visitor flow | Tap with in-button Sent, later owner-chat delivery; hold/release USR1 voice note; USR2 sleep/wake. | Basic tap, voice and sleep/wake have recorded hardware results; detailed cooldown/recovery checks remain pending. |
| Check operating limits | Clock, public display and connectivity considerations | Instruction blocks distinguish expected behavior, hardware evidence and pending checks. | Copy complete. |

Firmware configuration is nested only in `flash_door_sign.config`: SKU `100069905`, type `57`, bin mode, device-default `flash_address: "0x0"`, `muse_gadget: true`, `full_flash: false`. The required application's link address is `0x20000`, independently confirmed by the standard `esp32/partitions_muse.csv` OTA-0 row. Hub's Muse path supplies the device segments. The artifact fields remain empty strings pending the reviewer's upload; sizes will be byte-count strings. SDK tokens are supplied by each deploying user through the page prompt.

The full-UI alternative for a reviewer is to send the skill with `tools/muse/chat.py --file`; `esp32/tools/muse/chat.py:303,315-318` reads the file text. The primary page journey uses Muse chat paste. No chat helper was executed during this task.

## Artifact ledger

| Artifact | Local path | Role / destination | Existence, size and SHA-256 |
|---|---|---|---|
| Release firmware | `/Users/mengdu/Desktop/mgc-release/door-sign-0a1381a/muse-gadget.bin` | ESP32-S3 Muse application linked at `0x20000`; destination `flash_door_sign.config.firmware_url`, `.firmware_name`, `.firmware_size`; planned firmware upload type 4 | Present, 3018752 bytes; SHA-256 `b23689fd054e62e0fb32114f97ce32f0ebe192deb6be7f3cfeeeb6d9a0edc0f7`; complete-token scan passed. Revision label `0a1381a` matches the inspected source HEAD prefix. |
| Muse skill | `projects/door-sign/SKILL.md` | Full text installed into Muse chat; linked from the page; no artifact upload selected | Present, 6853 bytes; SHA-256 `9895b648389a38a2325f4307d8e29cf0582d8d1ca20dfb2309b70acc94318d39` |
| Project guide | `projects/door-sign/README.md` | Source/setup evidence and public source link; no artifact upload selected | Present, 8962 bytes; SHA-256 `657c3d7051aa501d8e48197b2bba60b701a76f2c7d6f9ee4ae4daa0f0089c5fa` |
| Chinese guide | `projects/door-sign/README.zh-CN.md` | Local project evidence and license check; no artifact upload selected | Present, 8750 bytes; SHA-256 `1325086a9c9229a814473a371a015e9d32d3afe442ed59ce9afc8d36461ffca5` |
| Verification record | `projects/door-sign/VALIDATION.md` | Hardware-claim authority, linked from the page; no artifact upload selected | Present, 11276 bytes; SHA-256 `2ddc0bc0fe0f6e20f3e70668aad6c039678d6c3500501d66ce5e9726be4a4606` |
| Cover/gallery | None selected | Cover before publication; optional gallery later | Pending owner photos. |

The owner-established release specification is an empty-SDK-token rebuild linked at `0x20000`. The release appeared by final inspection and was read without changing the build or its output. Its size and modification timestamp were stable across the read, and its ESP image magic is `0xe9`. Target and link-layout evidence are the owner-established release specification, the XIAO ESP32-S3 overlay and the standard Muse partition table. The active build's configuration was left untouched.

The exact ledger file independently passed both a quiet Python complete-token pattern check and the prescribed quiet command:

```sh
grep -aqE 'mgst_[A-Za-z0-9_-]{43}' /Users/mengdu/Desktop/mgc-release/door-sign-0a1381a/muse-gadget.bin
```

Result: exit 1, no complete token pattern found, no matching values printed. Before a later upload, compare the selected bytes with the ledger size/SHA-256 and repeat this scan. A scan pass means exit 1, a complete-token match means exit 0, and exit 2 or above means a check error. Report only the outcome. Upload metadata remains empty in the manifest for the reviewer to fill from the upload result.

## Recorded validation boundary

`projects/door-sign/VALIDATION.md` records the compact layout on 2026-11-04 in this branch, based on `e62777b`. It gives one physical XIAO 1.47-inch unit and an unspecified hardware revision, rather than a complete final reviewed release commit.

- Recorded hardware passes: Muse sets the sign; reply tap reaches Muse and Muse replies to the owner; deadline returns the sign to Free; sharp text; complete labels in the compact layout; USR1 visitor voice note; USR2 sleep/wake.
- Recorded hardware pending: sign persistence after reboot. The detailed remaining review also includes pairing/Wi-Fi text entry/reconnect, all sign commands, exact tap owner/time and cooldown, return from voice/settings, midnight deadlines and expiry notice count, power-cycle restore after clock sync, and outage/event recovery.
- Recorded toolchain: ESP-IDF v6.0.1, Xtensa GCC 15.2.0, macOS 26.6.2; compact-layout firmware app size `0x2e1000`, 28% app-partition space free. These are prior recorded results, not fresh build evidence.
- Recorded focused host checks: 9 tests passed. Recorded compact-layout simulator checks: 13/13 passed. An earlier larger-font full suite had 772 tests, 8 environment errors and 5 skips; the record lists the missing prerequisites and compiler issues. This manifest task did not rerun firmware or simulator suites.
- Final release-image identity, Hub browser flashing/token entry, first-time pairing and the resulting device behavior remain unverified for this draft.

## Local checks and editorial result

Reader-facing Markdown was compiled with the local `sensecraft-cli app doc compile --from-md` command. Compiler output had `warnings: []` and `pending_uploads: []`; the latter covers text/media compilation, while firmware metadata remains independently pending. Compiled HTML/plain-text blocks were inspected and merged with the firmware tool at the flash step. The excerpt uses 175 UTF-16 code units, within the 300-unit limit.

Editorial review of the assembled name, excerpt, introduction, step titles, instructions and BOM note passed: hardware claims trace to the validation ledger; the complete first-time journey and skill installation are present; examples explain outcomes; routines, queue delivery and reboot limits are qualified; prose has compiled links and no raw Markdown. The manifest links the supplied public project folder and SKILL.md directly. Link targets map to existing local project files; anonymous public accessibility remains an offline verification gap.

Required format check:

```sh
python3 /opt/homebrew/lib/node_modules/@seeed-studio/sensecraft-cli/skills/sensecraft-cli/create-app/assets/validate_manifest.py codecraft-app.door-sign.json --scene-ids 1,2,3,4,5,6,7,8,999
```

Result: exit 0, `PASS`, 3 expected warnings: no images, firmware URL pending and firmware URL empty. The two URL warnings describe the same pending upload. Structural validation also checks recognizable credentials without printing their values; the separate binary scan above checks the selected firmware bytes. Neither check executes the deployment.

Additional local acceptance checks passed: unique block IDs, exact approved BOM identity and permissions, one correctly placed nested firmware tool, known links mapping to existing project files, English-only manifest content, private local synchronization state, complete-token patterns absent from both deliverables, and unchanged SHA-256 values for all four project documents. `git diff --check` passed. Final status lists only the two new uncommitted deliverables.

| Review dimension | Result |
|---|---|
| Manifest format | Passed with 3 expected warnings. |
| Catalog identity | Reviewer API-confirmed on 2026-11-05; exact supplied row retained. |
| Editorial and evidence review | Passed for the local draft. |
| Deployment inputs | Incomplete: firmware upload URL, returned filename and byte-size string pending; local release exists and passed its token scan. |
| Publishing inputs | Incomplete: cover pending; final release Hub/device test and public-link check pending. |
| Remote state | Local-only; app creation, upload, saved BOM and activity association have not occurred. |

## Pending inputs and continuation

1. Recheck the completed `door-sign-0a1381a/muse-gadget.bin` against the artifact ledger immediately before upload. Its current size, digest and clean complete-token scan are recorded above; record any subsequent release replacement as a new reviewed artifact.
2. Upload that reviewed firmware and fill `flash_door_sign.config.firmware_url`, `.firmware_name` and `.firmware_size` from the result. The current local byte count is `3018752`. Remove the temporary reader-facing pending-attachment sentence when the attachment is available. Revalidate the canonical manifest.
3. Add the selected cover before publishing; gallery images remain optional. Account/author may be completed from verified authentication identity during the later online session.
4. Verify the supplied source and skill links while signed out. After a later upload/create, record the app ID immediately, verify saved blocks/BOM, and retain the verified synchronization baseline.
5. Test the exact uploaded firmware through the Hub page, including personal token provisioning, pairing and main sign interactions. Record the final revision, board revision and unit count, and complete the pending user-relevant hardware checks before claiming those paths verified.

The requested local authoring work is complete. Firmware attachment and publication are subsequent reviewer actions; this manifest is not yet deployment-ready.
