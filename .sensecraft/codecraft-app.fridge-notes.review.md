# Fridge Notes manifest review

## Scope and source snapshot

- Review date: 2026-11-05 (Asia/Shanghai).
- Manifest: [codecraft-app.fridge-notes.json](../codecraft-app.fridge-notes.json), revision 1.
- Worktree: `/Users/mengdu/Desktop/mgc-sticky`; branch: `project/fridge-notes`.
- Inspected source revision: `a8334df9c4e112085d6ed87fe61a17ac7ea6d3bf`.
- Initial working tree was clean. Git repository detection returned `true`.
- This delivery prepares two local files for review. App identity and creation time remain unset; the last-pushed revision remains 0. Remote creation, uploads and saved-state verification are future operations.
- Work was limited to local reference reads, project inspection, text compilation and manifest validation. The existing ESP32 build environment, running builds and serial ports were left untouched. Changes remain uncommitted.

## Authoring references

Read in full before drafting:

- `/opt/homebrew/lib/node_modules/@seeed-studio/sensecraft-cli/skills/sensecraft-cli/create-app/SKILL.md`
- Its `references/project-discovery.md`, `references/content-review.md`, `references/manifest.md` and `assets/manifest-template.json`.
- The parent `sensecraft-cli/SKILL.md` and its `references/app-fields.md`, `references/tool-blocks.md`, `references/app-commands.md` and `references/files.md`.
- Repository `AGENTS.md`, `challenge/AGENTS.md` and `challenge/README.md`.

The local `sensecraft-cli app fields` and `sensecraft-cli app blocks` commands both succeeded. Their static schemas confirm field shapes, flat block order, nested tool configuration and the Sticky Muse settings. Scene selection, catalog matching and environment facts below come from the supplied review evidence.

## Evidence and claim boundaries

| Fact | Evidence | Review finding |
|---|---|---|
| Established name and family use | [Project README](../projects/fridge-notes/README.md), opening and Things to say to Muse | Preserve Fridge Notes; address families and makers with a Sticky and Muse account. |
| Full-UI Sticky target | `esp32/devices/sdkconfig.reterminal-sticky`, `CONFIG_IDF_TARGET="esp32s3"`, `CONFIG_MUSE_BOARD_RETERMINAL_STICKY=y`, `CONFIG_MUSE_STICKY_NOTES=y` | Target ESP32-S3, Sticky full Muse/FreeInk UI. |
| Events and clock selection | `esp32/main/Kconfig.projbuild`, `config MUSE_STICKY_NOTES` | The Notes feature selects gadget events and clock, gated by Sticky and daemon requests. |
| Six live commands | `esp32/main/sticky_notes_model.c`, `sticky_notes_register`; gated registration in `esp32/main/noise_control.cpp` and dispatch in `esp32/main/app.c` | `notes.get`, `notes.add`, `notes.set`, `notes.toggle`, `notes.remove`, `notes.clear_done` are implemented. |
| Capacity and strings | `esp32/main/sticky_notes_model.h` and project SKILL.md | Four lists, twenty items each; item text 127 UTF-8 bytes, title/person 31 bytes, slug 23 bytes. Copy explains multibyte limits. |
| Local storage and mirroring | `esp32/main/sticky_notes.c`, `store`, `load`, `DEBOUNCE_US`, `RETRY_US`; `sticky_notes_write_body` and `STICKY_NOTES_PATH` | NVS persistence; one-way mirror to `gadget/fridge-notes/lists.json`; approximately three-second settle period and thirty-second retry. These are implementation facts with pending live checks. |
| Pairing and first use | Project README, Build, provision and install the skill; root README, Step 1 and Step 3 | Own SDK token, phone Muse account, Bluetooth, Developer mode, Add Device, matching MuseGadget name, AI-button confirmation, 2.4 GHz Wi-Fi. |
| Skill installation | [SKILL.md](../projects/fridge-notes/SKILL.md), project README step 4, `skills/README.md`; supplied owner decision | Paste the full skill once and ask Muse to remember it; repeat after changes. Optional USB file-send command is documented for this full-UI board. |
| USB file-send alternative | `esp32/tools/muse/chat.py`, module documentation and `--file` support | Documented command from `esp32/`: `python3 tools/muse/chat.py --file ../projects/fridge-notes/SKILL.md`. This command was inspected, not executed. |
| License | Apache headers in project README.md, SKILL.md, VALIDATION.md and `esp32/main/sticky_notes.c`; SPDX in Sticky overlay | `Apache-2.0`. Platform clone and commercial settings are separate explicit owner choices. |
| Recorded device results | [VALIDATION.md](../projects/fridge-notes/VALIDATION.md), On-device (2026-11-04) | One factory Sticky, CH343 console: Online/Wi-Fi connected; Muse created four lists, added Eggs/Milk, read both open, then marked Eggs done with Milk still open. |
| Hardware gaps | VALIDATION.md, Reviewer build and hardware checks and On-device | Touch toggles, AI-button voice, live VM mirror and sleep/wake were explicitly unexercised. Final-image pairing/reconnect, Wi-Fi editor, full navigation, reboot storage, near-limit capacity, notifications and battery behaviour still need checks. |
| Historical automated results | VALIDATION.md, Automated checks | Fifteen focused tests passed. The full run recorded 814 tests, 1 failure, 5 errors and 7 skips, with existing environment/baseline issues. These are historical evidence, not rerun results. |
| Tested-source boundary | VALIDATION.md describes `b227353` plus its then-uncommitted patch | The historical hardware result is not asserted as verification of the current HEAD or the eventual uploaded release image. |

The README's broad awaiting-validation paragraph predates the explicit On-device entry. App copy uses the latter for the narrow confirmed list-command results and retains the other pending checks. Earlier sandbox build limitations in VALIDATION.md and the owner's later release-build statement are recorded as distinct evidence.

## Decisions

| Item | Resolution and evidence |
|---|---|
| Language and audience | English; families and makers who own reTerminal Sticky and a Muse account, explicitly settled by the owner. |
| App name and excerpt | Final values live in `info` in the manifest. Name preserved from the project; excerpt describes shared lists and Muse-driven edits without promising unverified voice or offline behaviour. |
| Scene | String `"2"`, supplied by the owner. Validator allowlist supplied for this task: `1,2,3,4,5,6,7,8,999`. No scene label is inferred from that ID. |
| Permissions | `allow_clone: 1`, `allow_commerce: 0`, explicitly settled by the owner. |
| Site and organization | Global, organization `424326273238784`, active SenseCAP Access Key reported by the owner. Offline scope uses that evidence; no auth command was run and no key value was collected. |
| Account and author | Authenticated account identity was not supplied. `identity.account` remains null and `info.author` remains an empty string under the manifest contract. Optional identity metadata can be supplied during the later authenticated workflow. |
| Activity | None; `info.activity_id` is null. The reviewer reported a successful empty `app activity list` on Global on 2026-11-05, and the owner settled no activity. Exact time was not supplied. There are no candidate IDs, guide/rules links or join result to record. |
| Images | Empty local image list, explicitly accepted for this private draft. Photos come later; a cover is required before publishing. A future empty-image payload omits both `images` and `pic_url`. |
| Remote lifecycle | Local-only, intended private draft. `identity.app_id` and `created_at` remain null; no remote app or publication is claimed. |

## BOM evidence

The reviewer API-confirmed the following exact product through `app bom search` on Global on 2026-11-05 and supplied it in the task. The exact search argument and time-of-day were not supplied. This session preserves that confirmation and performs no catalog query.

| SKU saved as id | Catalog name | Quantity | Purchase URL | Role |
|---|---|---|---|---|
| `100056818` | reTerminal Sticky | `"1"` | https://www.seeedstudio.com/reTerminal-Sticky-p-6861.html?___store=retailer | Runs Fridge Notes and displays shared household lists with checkboxes. |

Quantity and role follow the single-board project README, which adds no peripherals. SKU, name, URL and quantity match the supplied row exactly. This is the sole BOM row; the USB cable, phone and desktop computer are preparation requirements. No custom or unverified product rows are used. Remote BOM persistence awaits a later app upload and read-back.

## Deployment plan and tools

The manifest contains two introductory paragraphs followed by six steps. It has 36 blocks total, including one `flash-firmware` tool and one plain code example.

| Step | Prerequisites and action | Tool and input | Observable result and readiness |
|---|---|---|---|
| Prepare your device and accounts | Sticky, USB data cable, desktop Chrome/Edge, phone Muse app/account, Bluetooth, 2.4 GHz Wi-Fi and own SDK token | External token-management link; per-user inputs stay with the user | Hardware/accounts ready for the page's flash prompt. |
| Flash Fridge Notes | Connect CH343 console; use the adjacent tool, follow hard-reset/token prompts | `flash_fridge_notes`; SKU `100056818`, type `61`, bin mode, `flash_address: "0x0"`, `muse_gadget: true`, `full_flash: false` | Pending firmware attachment; the page explicitly shows this draft gap. Final image requires Hub-page device testing. |
| Pair with Muse | Add nearby MuseGadget in Muse, confirm with AI button, provide Wi-Fi | Muse app setup as documented in project/root README | Muse reports connected; final-image setup/reconnect still needs verification. |
| Install the skill once | Paste complete linked SKILL.md and ask Muse to remember it; send again after updates | Muse chat, with optional documented `chat.py --file` alternative | Ask Muse to create the four lists; first list becomes home. Clock setup supplies the local sync-label time. |
| Try your household lists | Typed add/read/complete requests, then quantity/assignment/cleanup examples | Muse's registered Notes commands; intended voice/touch operations explicitly qualified | Core add/read/complete path has historical device evidence. Wider operations await device checks. |
| Understand limits and verification status | Read capacity, shared visibility, persistence, stale mirror, speaker and routine constraints | Linked validation record | Reader sees the exact experimental limits and remaining final-image checks. |

The firmware tool immediately follows its flash instructions and draft-attachment notice, before the pairing step. Its settings occur only in `block.config`. Muse mode uses the Sticky default address `0x0`; the selected Muse application must be linked at `0x20000`, as the Muse rows require. The Hub page supplies the device-specific bootloader/partition/OTA segments and token prompt. Per-user credentials and extra segment files are not manifest fields.

## Artifact ledger

### Release firmware

| Field | Evidence / status |
|---|---|
| Selected local file | `/Users/mengdu/Desktop/mgc-release/fridge-notes-a8334df/muse-gadget.bin`, resolved from the owner-supplied `fridge-notes-<rev>` release path pattern. |
| Target | reTerminal Sticky full Muse/FreeInk UI, ESP32-S3; SKU `100056818`, uniform type `61`. |
| Owner's build evidence | Owner reports rebuilding with an empty SDK token, a clean complete-token scan, and application linkage at `0x20000`. |
| Current filesystem evidence | The release file appeared by final review and was read without changing the release or build directories. The folder suffix matches the inspected source revision's short hash. |
| Byte size | `2691072` bytes. |
| SHA-256 | `ec135588302ae051c2ab1c03382ea07d6674963b310b7f2e7ec460d78d25e8ad`. |
| Independent complete-token scan | PASS on the exact selected file: quiet grep returned exit 1, with no complete SDK token pattern found. |
| Destination | `deployment.blocks` tool ID `flash_fridge_notes`, `config.firmware_url`, `config.firmware_name`, string `config.firmware_size`. |
| Upload state | Unuploaded in this run; all three firmware metadata values are empty strings. The reviewer supplies the actual upload result. |

Use this reviewed image for the later firmware upload and recheck its hash and quiet token scan immediately before uploading. The quiet scan is `grep -aqE 'mgst_[A-Za-z0-9_-]{43}' <firmware-file>`: exit 1 passes, exit 0 reports a match, and exit 2 or above reports a check error. Record only the outcome. Feature selection and target were inspected in source; empty build-token configuration and `0x20000` linkage remain supported by the owner's supplied build statement. Existing build outputs were left untouched.

### Local source documents

These inspected files are evidence and linked project content; they are not planned firmware uploads.

| Path | Bytes | SHA-256 | Use |
|---|---|---|---|
| `projects/fridge-notes/README.md` | 8699 | `14935696ef59218636ccf75c102e9e82cbd1224f97a0df0f097ea13674a80189` | Project purpose, setup, usage and limits. |
| `projects/fridge-notes/SKILL.md` | 9459 | `c4445bb4f8a39645d7a1da8b7a67d5bc1c016819fd01ce879052ef659484be95` | Full chat installation and optional USB file-send input. |
| `projects/fridge-notes/VALIDATION.md` | 8621 | `852ccd9fbcb27a740120628a1802a064e770adbe3e49c3c7f974b31645ed40f6` | Recorded test results and hardware gaps. |

No image candidates were selected or hashed. Public source, README, skill, validation and token-management links were inspected for shape and destination against local evidence and the supplied URLs. Their live availability, including signed-out GitHub access, awaits online review.

## Content and format review

- Truth: capability claims trace to the inspected implementation and project docs. Hardware-tested claims are limited to the On-device entry. Intended voice, touch, mirror and sleep behaviour is qualified with pending checks.
- Clarity: prerequisites precede flashing; the user supplies their own SDK token through the page; the Sticky's AI button confirms Muse pairing. The skill installation asks for the complete file and explains updates.
- Flow: prepare, flash, pair, install skill, use lists, then limits. Typed smoke-test examples match recorded list behaviour; their wording is illustrative rather than a transcript.
- Economy: two short introductory paragraphs, concrete step titles and one relevant tool. Technical details are retained where they affect capacity, setup or stale/offline data.
- Presentation: reader-facing Markdown was compiled with the local CLI. Compiled links have HTML attributes and text blocks contain editor HTML/plain text. Tool order and nested config were checked after assembly.
- Security: manifest text passed the validator's credential checks; account metadata stays unset, and no key or token value was placed in either deliverable. The selected release binary passed the independent quiet complete-token scan.
- Pending-string handling: firmware URL, name and size use empty strings, matching the validator's pending-value contract.

## Checks run

```sh
sensecraft-cli app fields
sensecraft-cli app blocks
sensecraft-cli app doc compile --from-md /private/tmp/fridge-notes-hub-copy.md
python3 /opt/homebrew/lib/node_modules/@seeed-studio/sensecraft-cli/skills/sensecraft-cli/create-app/assets/validate_manifest.py codecraft-app.fridge-notes.json --scene-ids 1,2,3,4,5,6,7,8,999
grep -aqE 'mgst_[A-Za-z0-9_-]{43}' /Users/mengdu/Desktop/mgc-release/fridge-notes-a8334df/muse-gadget.bin
git diff --check
```

- Local schema/catalog inspection succeeded.
- Markdown compilation succeeded: `warnings: []`, `pending_uploads: []`. This compiler result covers the authored prose; the pending firmware tool was inserted afterward at its intended position.
- Manifest validator: exit 0, `PASS: codecraft-app.fridge-notes.json (3 warning(s))`. Warnings: no images, pending firmware URL, and empty firmware URL. The latter two identify the same missing deployment attachment.
- Additional local assertions passed for source/skill links, link attributes, tool placement, matching BOM SKU and flash SKU, type `61`, address `0x0`, boolean flags, English manifest copy and unpushed identity/sync state.
- The exact release file's size and SHA-256 were recorded. Its quiet complete-token scan passed with exit 1.
- `git diff --check` passed. Project document hashes were computed from local bytes.
- Build, firmware execution, hardware checks and historical host tests were not rerun in this manifest-only task.

## Pending inputs and review result

1. **Firmware attachment:** the selected local image is available and scanned clean. After the reviewer uploads it, fill the flash tool's URL, returned filename and string size. Remove the temporary reader-facing attachment-pending notice once those values are present.
2. **Final-image hardware verification:** exercise this exact image through the Hub draft page, including SDK-token provisioning, pairing/reconnect and the core list commands. Record app ID, image identity, source revision, device revision/unit count and results; expand the pending interaction checks listed above as hardware becomes available.
3. **Publishing assets:** supply an authorized cover photo before publishing; add gallery images if desired.
4. **Online checks:** confirm signed-out public source/skill/validation links and, during the later cloud workflow, the active account/environment and saved app/BOM values. Optional account/author metadata remains unspecified.

**Review result:** editorial review passed; format validation passed with three expected warnings; BOM catalog identity is confirmed by the supplied reviewer evidence. This is an authorized, incomplete local private-draft manifest. Deployment inputs remain incomplete, and publication needs a cover. No upload, app creation, saved-state verification or publication occurred in this run.
