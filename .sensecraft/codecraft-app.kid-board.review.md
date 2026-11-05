# Kid Board Hub manifest review

Review date: 2026-11-05. Canonical manifest: [codecraft-app.kid-board.json](../codecraft-app.kid-board.json).

## Evidence and scope

- Worktree: `/Users/mengdu/Desktop/mgc-kid`; branch: `project/kid-board`; inspected HEAD: `7cd1510e0b520c8d02ef20f3c23a0f9f09f75752`. The worktree was clean at discovery. This batch adds the manifest and this review, with changes left uncommitted.
- Project evidence: [README](../projects/kid-board/README.md), [Muse skill](../projects/kid-board/SKILL.md), and [validation record](../projects/kid-board/VALIDATION.md), each read in full. Root and challenge instructions and the English participant guide were inspected.
- Public project folder, supplied by the owner: https://github.com/limengdu/muse-gadget-challenge/tree/project/kid-board/projects/kid-board . The page links directly to it, the full skill and the validation record. Signed-out remote availability remains a reviewer check because this run is offline.
- Required CLI readings were completed in full under `/opt/homebrew/lib/node_modules/@seeed-studio/sensecraft-cli/skills/sensecraft-cli/`: `create-app/SKILL.md`, `create-app/references/project-discovery.md`, `create-app/references/content-review.md`, `create-app/references/manifest.md`, `create-app/assets/manifest-template.json`, `SKILL.md`, `references/app-fields.md`, `references/tool-blocks.md`, `references/app-commands.md`, and `references/files.md`.
- Execution consisted of local file inspection, artifact hashing/scanning, static CLI inspection, document compilation and manifest validation. Cloud operations, firmware compilation, device flashing and serial access are outside this batch.

## Settled decisions

| Item | Evidence and disposition |
|---|---|
| Name | The README title is retained in `info.app_name`. The manifest is the authoritative copy of final app values. |
| Language and audience | Owner-selected English; families and makers who own an E1002 and a Muse account. |
| Scene | Owner-selected ID `6`, stored as a string. Allowed IDs supplied for local validation: `1,2,3,4,5,6,7,8,999`. A scene label was not inferred from an offline catalog. |
| Permissions | Owner-selected `allow_clone: 1` and `allow_commerce: 0`. Platform permissions are explicit and separate from the source license. |
| Environment | Owner-confirmed Global site, org `424326273238784`, active SenseCAP Access Key. Account identity was not supplied, so `identity.account` remains null and optional `info.author` remains empty. |
| Activity | The reviewer reported that Global `app activity list` returned an empty list on 2026-11-05. No activity is selected; `info.activity_id` is null. This is a supplied successful empty result, rather than a failed lookup or a new live query. |
| License | Apache-2.0 confirmed from root `LICENSE` and Apache headers in all three project evidence files plus `esp32/main/kid_board.c`, `kid_model.c` and `kid_store.c`. |
| Images | Owner-selected empty image list for a local private-draft preparation. Publishing requires a cover; real device photos will be supplied later. |
| Remote identity and sync | New local manifest: app ID and created time remain null, local revision 1, last pushed revision 0, pushed time/hash null. No remote app or verified remote baseline exists in this workflow. |

## Capability and hardware evidence

| Page claim | Source and review finding |
|---|---|
| E1002 target and six-colour 800×480 screen | Project README; `esp32/devices/sdkconfig.reterminal-e1002` enables Spectra 6, ESP32-S3 and Kid Board. `esp32/main/Kconfig.projbuild` scopes Kid Board to that backend and selects clock, events, card rendering and board IO. |
| Complete daily-plan replacement, stable IDs, ten items, local dates, star totals/goals and remote correction | `esp32/main/kid_model.c`, `kid_model.h`; command registration in `esp32/main/noise_control.cpp:1499` and command routing in `esp32/main/app.c:1884`. The skill describes state-first edits, permissions and the daily cap. |
| Physical Green confirmation, tone and owner notice | `kid_board.c:136`, `kid_board.c:173`, `kid_model.c:226`; the latest hardware section of VALIDATION records the beep, tick, posted `kid.done` and owner's Muse notification on one E1002. |
| Plan, stars and reward display | VALIDATION, “On the device — 2026-11-05”: one paired E1002 over home Wi-Fi, skill pasted into Muse, `clock.set`, `kid.set_schedule` and `kid.set_stars` called successfully, screen layout checked by the owner. This verifies the displayed reward label; the earned-reward page remains a separate pending check. |
| Slow screen and combined updates | README and `kid_board.c:204` worker; `kid_model.h` defines a 60-second redraw interval. VALIDATION records a 26-second full refresh. The page describes the implemented spacing without claiming a full physical timing acceptance run. |
| Latest idle observation | The top hardware record reports one redraw on restore at boot, none in the following 4.5 minutes and 2,136 bytes free on the `epd` stack. It is the latest supplied device evidence for this narrow observation. |
| Earlier refresh-patch record | The following historical section identifies `e35aa88` plus a patch and states zero hardware units for that patch at the time. Its source/test fingerprint is `12732e4116fc36c5376acc1d3195febaca5b3b2a001c0da6ef05b6ee3f570a53`; recomputing the documented sorted path/NUL/content/NUL digest over the ten listed current files matches it exactly. The later top hardware note supplements that record without establishing all follow-up checks. |
| Recorded automated results | The refresh section reports 48 focused/shared e-paper tests passed, passing E1002/E1001 builds with ESP-IDF v6.0.1 / Xtensa GCC 15.2.0, and a full suite of 780 tests with 1 failure, 8 errors and 7 skips tied to recorded environment/fixture limits. These are prior project results, not checks executed in this authoring batch. |
| Persistence, future plans and daily rollover | Implemented in `kid_model.c`, `kid_store.c`, and `kid_board.c`; covered by recorded host tests. The public page labels power-cycle restoration and midnight rollover as pending device checks. |
| History and caregiver rules | README and skill: recent 32 notices live in bounded RAM; connectivity and power affect delivery; Muse memory holds the caregiver agreement, award ledger and saved daily summaries. The board stores one dated plan and absolute star totals. |

Hardware claims are limited to VALIDATION's one E1002. Its physical board revision and exact source/binary revision for the latest device run were not recorded. The source fingerprint match establishes inspected-source correspondence with the patch record, not proof that this exact release binary passed a device test.

## Deployment plan and tool decisions

The compiled document has two introduction paragraphs, six steps and one tool, with 26 flat blocks in total. Each step has explicit preparation/actions and an expected result where applicable.

| Step | Inputs and action | Tool or mechanism | Expected result and readiness |
|---|---|---|---|
| Gather what you need | E1002, USB data cable, desktop Chrome/Edge, online Wi-Fi, Muse app/account and personal SDK token from `gadgets.muse.ai` | Reader preparation | Required items are listed before use; the user's own token is supplied at deployment time. |
| Flash Kid Board firmware | Connect USB; use the immediately following flash block; follow reset/token prompts | `flash-firmware`, block ID `flash_kid_board` | Clean release image exists locally. Firmware URL/name/size await reviewer upload; the local draft is incomplete for deployment. |
| Pair the board in Muse | Add nearby `MuseGadget-Disp-…`, configure Wi-Fi and confirm with Green | Muse app and board button, per project README | Muse connects to the identified E1002. |
| Install the Muse skill once | Paste the full linked `SKILL.md` into Muse chat, ask Muse to remember it, resend after changes, supply family/time-zone/star agreement | Muse chat and memory | Muse has the command instructions and agreement; a state-read request checks connection/time settings. |
| Plan a day and confirm a step | Use the recorded short routine example, press Green, try documented edits/awards/state/correction requests | Muse commands and Green | The checked routine and Green path are distinguished from additional supported requests awaiting full end-to-end device checks. |
| Check operating limits and validation | Review capacity, date replacement, screen delays, event history, permissions and actual hardware status | Reader-facing limits and validation link | User expectations match evidence and outstanding checks. |

The flash block keeps SKU `100017057` distinct from `uniform_type` `62`, uses `flash_mode: bin`, `muse_gadget: true` and `full_flash: false`, with all settings inside one `config`. The tool reference requires E1002's page-default `flash_address: 0x90000`; Muse mode supplies the standard multi-segment layout with the app at `0x20000`. The owner confirms that the selected `muse-gadget.bin` is linked at `0x20000`. This link address was not independently reconstructed from an ELF because the release folder contains only the binary. Ancillary flash segments and personal token handling belong to the Hub page.

Copy was authored as reader-facing Markdown in a temporary local file and compiled with `app doc compile`. The flash tool was inserted directly after its instructions, before the pairing step. Compiled links use editor-compatible HTML; prose contains no raw Markdown markers. Public tool configs contain remote-value slots only; artifact filesystem paths remain in this review.

## BOM evidence

The owner supplied the reviewer's API-confirmed selection from Global `app bom search` on 2026-11-05. The exact `--name` query and raw API response were not supplied. No new cloud search was run in this offline batch.

| Status | SKU saved as `id` | Catalog name | Quantity | Catalog purchase URL | Project role and quantity evidence |
|---|---|---|---|---|---|
| API-confirmed by reviewer | `100017057` | reTerminal E1002 | `1` | https://www.seeedstudio.com/reTerminal-E1002-p-6533.html?___store=retailer | One E1002 runs the daily timeline and star chart; Green confirms a step and sends a Muse notice. README targets this device, and VALIDATION records one unit. |

This exact single catalog row is used. Generic USB cable/computer requirements appear in prerequisites. Catalog price and picture are resolved by SKU by the platform. Remote saved-row verification belongs to the later upload/create workflow; this batch establishes local authoring from the supplied catalog evidence.

## Artifact ledger

| Property | Recorded value |
|---|---|
| Role | Muse Gadget application image for Kid Board on reTerminal E1002 / ESP32-S3 |
| Local path | `/Users/mengdu/Desktop/mgc-release/kid-board-7cd1510/muse-gadget.bin` |
| Existence | Present and readable; the release directory contains only this file |
| Size | `1642496` bytes (`0x191000`), matching the E1002 app size recorded in VALIDATION |
| SHA-256 | `903bbe5956e337e056954a2182ffc9c8c0c069b7712cc25c0aa04c6a9738404f` |
| Header inspection | ESP image magic `0xe9`, image-header chip ID `9`, application descriptor project name `muse-gadget`; header metadata alone does not identify the project source revision |
| Build provenance | Owner-supplied release path tied to `7cd1510`; owner confirms an empty SDK-token rebuild and app link address `0x20000`. A release sdkconfig/ELF was not supplied, so those build facts are recorded as owner-confirmed. |
| Credential scan | Exact binary scanned locally using `grep -aqE 'mgst_[A-Za-z0-9_-]{43}'`; exit code `1`, PASS. An independent byte scan also found no complete Muse SDK-token pattern. Matching values were never printed. |
| Destination | `deployment.blocks` tool `flash_kid_board`, `config.firmware_url`, `config.firmware_name`, `config.firmware_size` |
| Upload state | Pending. All three destination fields are empty strings until the reviewer uploads the exact reviewed bytes and inserts the returned URL/name/string byte size. |
| Planned file category | Firmware, type `4`, within the documented 100 MB limit |
| Release device test | Pending Hub-page flash, personal-token provisioning, pairing/reconnect and main-function checks of this exact image |

Selected cover/gallery assets: none. No image hashes or URLs are available yet.

## Pending inputs and follow-up checks

1. **Deployment blocker:** upload the ledger's exact firmware bytes in the reviewer's workflow; fill `flash_kid_board.config.firmware_url`, `firmware_name` and string `firmware_size`, then validate again. Recheck the hash and quiet complete-token scan for the upload-selected bytes.
2. **Publishing input:** provide and review a real cover image; upload it as the first ordered app image. Photos/gallery are currently deferred by the owner.
3. **Verification:** test this exact uploaded release through the Hub flash page, including personal-token setup, pairing/reconnect, routine display, Green confirmation and owner notice. Record the app ID, firmware hash/name, source revision, board revision and unit count.
4. **Hardware limits:** exercise next-item timed redraw, earned-reward page, local-midnight rollover and plan restoration after a power cycle. Complete the additional skill-request paths, future-date behavior, RTC/time-zone changes and rapid-update physical refresh checks before describing them as hardware-verified.
5. **Publication checks:** open the project/skill/validation source links while signed out. Resolve optional account/author identity from verified account information when available. Global/org and the successful empty activity-list result are supplied reviewer facts for this offline authoring run.

Personal SDK-token entry is an intended deployment-time input, supported by the Muse flash block. It is separate from the missing authoring-time firmware URL. Cover availability affects publishing; firmware availability affects deployment.

## Editorial review result

- **Truth:** capabilities are traced to the README, skill and implementation. Hardware statements name the one-unit validation record; the latest narrow idle result, historical host/build evidence and pending release-image test are distinguished.
- **Clarity:** prerequisites precede USB use, pairing names the advertised device and Green button, and skill installation explicitly means the entire file once plus a resend after updates. Sample requests describe visible or chat outcomes.
- **Flow:** preparation → adjacent flash tool → pairing → skill/owner rules → routine/confirmation/use examples → limits. Source and skill links are present in public content.
- **Economy and presentation:** two short introduction paragraphs; action-only step titles; one necessary tool; English reader-facing text and concise BOM role; compiled HTML links and emphasis inspected.
- **Format:** validation result is recorded below. Format acceptance and editorial acceptance apply to the intentional local draft; deployment inputs remain incomplete and publishing still needs a cover.
- **Catalog:** exact row accepted from the reviewer's 2026-11-05 API evidence, without a new live lookup. Saved remote rows and public source accessibility remain unverified locally.
- **Security:** personal credentials are absent from the manifest/review; the exact release passes the complete Muse SDK-token scan. Optional author/account fields stay unset until verified.

## Local validation result

Local `sensecraft-cli app fields` and `sensecraft-cli app blocks` both exited `0` and their static output was inspected. Document compilation exited `0`, with `warnings: []` and `pending_uploads: []`; these arrays describe the compiled prose only, before the intentionally pending firmware tool was inserted.

Required manifest check:

```sh
python3 /opt/homebrew/lib/node_modules/@seeed-studio/sensecraft-cli/skills/sensecraft-cli/create-app/assets/validate_manifest.py codecraft-app.kid-board.json --scene-ids 1,2,3,4,5,6,7,8,999
```

Result: **PASS**, exit code `0`, zero violations and three expected warnings: no images (a cover is needed for publishing), `config.firmware_url` pending, and the same firmware URL empty. The latter two warnings describe the same planned upload. Firmware name and string byte size remain intentionally empty for the reviewer to fill together with that URL.

Additional local assertions passed: 26 uniquely identified blocks; six steps; one correctly nested Muse flash block immediately after its instructions and before pairing; exact supplied BOM and settled decisions; HTTPS source/skill links with new-tab attributes; English-only manifest text; trailing-whitespace/newline checks; unchanged exact firmware hash/size; and a passing complete SDK-token scan. The quiet `grep` scan independently exited `1` as required.

Final readiness: **format validated; editorially reviewed; catalog identity accepted from supplied reviewer evidence; firmware upload pending; local draft not yet deployment-ready; cover required before publishing; exact release image awaiting Hub-page hardware verification**. All changes remain uncommitted.
