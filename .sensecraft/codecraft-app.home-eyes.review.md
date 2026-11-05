# Home Eyes manifest review

Review date: 2026-11-05 (Asia/Shanghai). Manifest: `codecraft-app.home-eyes.json`.
Source branch: `project/home-eyes`. Inspected source revision:
`89dd206ca7fb1ff93de5f40ea3c8a1d5ef10608d`.

## Delivery and decisions

- Deliverable: an English local manifest and this review, ready for reviewer inspection. Remote creation, upload, and publishing remain separate stages.
- Name: **Home Eyes**, preserved from `projects/home-eyes/README.md:17`.
- Audience: families and makers who already own a SenseCAP Watcher and a Muse account (owner decision).
- Environment: Global, organization `424326273238784`; an active SenseCAP Access Key was reported by the owner. Account identity remains unknown, so `identity.account` is null and `info.author` is an empty string.
- Scene: string `"5"`, supplied by the owner. Validation uses the owner-supplied allowed IDs `1,2,3,4,5,6,7,8,999`.
- Permissions: `allow_clone: 1`, `allow_commerce: 0` (owner decisions).
- License: `Apache-2.0`, verified against root `LICENSE` and the Apache headers in all four project documents and `esp32/main/home_eyes.c`.
- Images: empty by owner choice for this private draft. Add a cover before publishing; gallery photos may follow.
- Activity: none; `info.activity_id` is null. The reviewer reported that `sensecraft-cli app activity list` returned an empty list on 2026-11-05 on Global. There were no available candidates or activity guides to assess. This session retains that supplied evidence; activity eligibility and association have no selected target.
- Identity and sync: fresh local draft, `app_id` and `created_at` null, local revision 1, last-pushed revision 0, last-pushed time/hash null. A verified remote baseline becomes applicable after a complete upload and readback.

## Project evidence

| Claim or input | Inspected evidence | Assessment |
|---|---|---|
| Camera look, fixed-scene watch, and family screen messages | `projects/home-eyes/README.md:21-24,61-108`; `projects/home-eyes/SKILL.md:26-110`; `esp32/main/home_eyes_core.c:103-187,218-253` | Implemented; hardware evidence is narrower, as listed below. |
| Device, target, camera, events, and clock support | `esp32/devices/sdkconfig.muse-sensecap-watcher:19-22,84-87`; project README setup | SenseCAP Watcher, ESP32-S3, existing onboard Himax camera path. Device `uniform_type` 37 is owner-supplied. |
| Runtime command integration | `esp32/main/noise_control.cpp:1595`; `esp32/main/app.c:1900-1901,2687`; `esp32/main/home_eyes_core.c:380-398` | Home Eyes registration, dispatch, and startup are present. |
| Full-flash and unit-specific factory backup | Project README setup; root `README.md:310-322` | Linked as preparation before changing original firmware. The browser Muse flash path preserves the factory area. |
| Pairing and Wi-Fi | Project README setup; root `README.md:324-349` | Nearby MuseGadget device, Developer mode, Add Device, wheel confirmation, 2.4 GHz Wi-Fi, connected state. |
| Skill installation | Project README setup; full project `SKILL.md`; owner deployment decision | Paste the complete linked skill into Muse chat once, ask Muse to remember it, and resend after changes. |
| Watch interval, single watch/message, verdict waits and acknowledgements | Project README commands/operation; project skill; `home_eyes_core.c:103-187,218-253`; `home_eyes.c:125-201` | Copy describes implemented behavior and identifies untested physical flows. |
| Photo handling, persistent text, event delivery and power limits | Project README operation/privacy; project skill limits; worker capture/save/event paths | Photos/thumbnails stay in RAM; settings and card text persist locally. Account retention follows Muse settings. USB power recommended; event delivery and battery limits remain explicit. |
| Authoritative hardware record | `projects/home-eyes/VERIFICATION.md` | The project's validation document is named `VERIFICATION.md`. Its later fixed-build subsection supplies the latest short-run measurement. |

### Hardware claims used in the page

- Baseline, one Watcher, board revision unknown: pairing, remote `camera.capture`, a `home.say` card, wheel acknowledgement with an event relayed by Muse, and `watch.start` worked (`VERIFICATION.md:19-43`). A later DMA shortage interrupted the baseline session.
- Fixed-build device result, one Watcher: about five minutes across two worker captures at a two-minute interval and low sensitivity; DMA free/largest 23K/17K, tunnel remained up, status replies and typed turns completed. The record reports a 640x480 JPEG capture in 0.90 seconds (`VERIFICATION.md:146-162`). The page uses the observed duration and successful replies, without turning them into a sustained reliability claim.
- Remaining device checks: one-hour run, one-time stack measurement, complete scene-change event to fresh photo to verdict cycle, reboot recovery, long-card scrolling, alert/reminder timing, and battery/sleep behavior (`VERIFICATION.md:136-162,310-325`).
- Recorded software results: 14 focused host tests passed with zero skips in the RAM follow-up; six target objects compiled with warnings as errors. Earlier full-suite failures, missing prerequisites and simulator download/build limitations remain recorded in the source validation document. These checks were read as evidence rather than rerun during manifest authoring.
- Test-revision boundary: the validation record attributes its follow-up to `a6367d3` plus its working tree, and separately records the fixed-build device result. It provides no exact release-image digest or fixed-device commit. Current source HEAD is recorded above; release equivalence still needs exact-artifact evidence.

## Deployment plan

| Order | Reader action and prerequisites | Tool / source | Expected result and readiness |
|---|---|---|---|
| 1 | Prepare one Watcher, Muse app/account, personal SDK token, 2.4 GHz Wi-Fi, data cable, desktop Chrome/Edge; retain unit-specific backups | Linked project setup guide and token settings page | Prepared device and per-user inputs. |
| 2 | Connect the Watcher's ESP32-S3/XIAO console port and write the release | One `flash-firmware` block, `home_eyes_flash_firmware`, immediately after the flash instructions | Firmware attachment pending. On completion, the page closes the port; the user replugs USB, then sends their own token using the page prompt. |
| 3 | Add the nearby MuseGadget device in Muse, confirm using the wheel, and supply home Wi-Fi | Muse phone app | Connected device shown in Muse and face visible on the Watcher. |
| 4 | Paste the full linked Home Eyes skill into Muse and ask it to remember it | Muse chat | One-time skill setup, repeated after skill updates. |
| 5 | Request a photo, start a fixed-scene watch, send a family message, inspect status, and stop checks | Muse commands supplied by the project skill | Observable photo answer, watch state, message card/read event. Complete watch-verdict and alarm paths remain hardware checks. |
| 6 | Review capacity, timing, privacy, delivery, power and validation limits | Reader-facing final step and linked validation record | Experimental scope and remaining physical checks are visible before unattended use. |

### Watcher flash configuration

- `device_id: "113991315"` selects the clear-enclosure representative of the Watcher group; the same Watcher firmware applies to the white-enclosure alternative. Both supplied SKUs are on the Muse allowlist.
- `device_type: "37"`, `flash_mode: "bin"`, `muse_gadget: true`, `full_flash: false`.
- `flash_address: "0x0"` follows the Watcher/XIAO editor default. The Muse app image itself is linked at `0x20000`; the page supplies Muse's four segments and their addresses. This distinction follows the fully read `tool-blocks.md` Muse rows.
- The Watcher path closes the port, asks for a USB replug, then sends the token. It preserves the unique factory partition. The block keeps configuration inside its single `config` object.
- `config.firmware_url`, `firmware_name`, and `firmware_size` are pending empty strings; the reviewer fills all three after upload, with size as a byte-count string.
- This project needs a firmware block and the Muse skill. Its local scene comparison and Muse photo inspection use the existing firmware path.

## BOM evidence

Evidence source: reviewer-provided API-confirmed selections from `sensecraft-cli app bom search` on 2026-11-05, Global. The handoff includes the exact selected SKU, name and purchase URL; original query arguments and raw responses were not retained in the supplied evidence. Catalog confirmation is attributed to that reviewer result. This authoring session performed local checks only.

| SKU saved as `id` | Exact catalog name | Quantity | Exact purchase URL |
|---|---|---|---|
| `113991315` | SenseCAP Watcher Clear Enclosure | `"1"` | https://www.seeedstudio.com/SenseCAP-Watcher-W1-A-p-5979.html?___store=retailer |
| `113991275` | SenseCAP Watcher White Enclosure | `"1"` | https://www.seeedstudio.com/SenseCAP-Watcher-W1-B-p-5980.html?___store=retailer |

Both rows are enclosure alternatives: **choose either enclosure (W1-A clear / W1-B white); one Watcher is enough**. This wording appears in the prerequisites and both BOM notes. Cable, computer, phone and ordinary USB power are prerequisites. Both BOM rows use catalog SKUs, and saved remote-row verification remains a later upload-stage check.

## Artifact ledger

| Artifact / destination | Path or source | Existence and exact-byte evidence | Next action |
|---|---|---|---|
| Muse Gadget release / `home_eyes_flash_firmware.config` | Owner-supplied pattern: `~/Desktop/mgc-release/home-eyes-<rev>/muse-gadget.bin`; expanded base `/Users/mengdu/Desktop/mgc-release/` | Base directory exists. Discovery found zero matching `home-eyes-*` directories. Exact selected path, size and SHA-256 are pending. | Select the completed release, record its revision/path/size/SHA-256, quietly scan those exact bytes, then upload as firmware type 4. Fill URL/name/string size in the intended block. |
| Release contract | Owner reports `muse-gadget.bin` rebuilt with an empty SDK token, linked at `0x20000`, and scanned clean | Supplied release status; this session has no release file to independently hash, inspect or scan. Target is Watcher/ESP32-S3. | Confirm the exact file and empty-token build evidence once the release is present; keep scan output to pass/match/error status. |
| Cover / `images[0]` | Future owner photos | Pending by decision; image size/hash/URL unassigned | Select a cover before publishing. App images support JPG/JPEG/PNG/GIF/WebP, each up to 5 MB. |
| Public project source | https://github.com/limengdu/muse-gadget-challenge/tree/project/home-eyes/projects/home-eyes | Linked in the introduction; corresponding local folder inspected | Reviewer checks anonymous GitHub visibility before publishing. |
| Skill | https://github.com/limengdu/muse-gadget-challenge/blob/project/home-eyes/projects/home-eyes/SKILL.md | Linked in introduction and install step; complete local file inspected | User pastes full contents into Muse chat. |
| Validation | https://github.com/limengdu/muse-gadget-challenge/blob/project/home-eyes/projects/home-eyes/VERIFICATION.md | Linked in introduction; complete local file inspected | Retain measured versus pending distinction as release checks progress. |

Local artifact paths stay in this ledger. Per-user SDK tokens, Wi-Fi credentials and access keys remain deployment-time inputs outside the manifest and review.

## Pending inputs and handoff

1. Completed release file at the supplied path pattern, including exact revision, byte size, SHA-256, and a quiet complete-token scan of the selected file. The owner-reported scan is retained as supplied evidence.
2. Uploaded firmware URL, filename and byte-count string for `home_eyes_flash_firmware.config`. Refresh the flash step's attachment-status sentence when the upload is complete. The current manifest is an incomplete local deployment draft.
3. Cover photo before publishing, followed by its byte metadata and upload URL. The current coverless draft is intentional.
4. Exact uploaded-release test through the Hub draft page, including Watcher USB replug/token provisioning, pairing, skill installation and main functions; complete the remaining device checks recorded above.
5. Anonymous access checks for the linked GitHub branch/files when network access is available. Local link targets and the setup heading are present.

## Review result

- Required reading completed: create-app skill, full project-discovery/content-review/manifest references and template; parent skill, full app-fields/tool-blocks/app-commands/files references. Project README, Chinese README, SKILL and VERIFICATION were inspected, with targeted implementation and overlay cross-checks.
- Static CLI checks passed: `sensecraft-cli app fields` and `sensecraft-cli app blocks` (zero-network).
- Content was authored for a first-time reader and compiled using `sensecraft-cli app doc compile --from-md <temporary-content.md>`. Compiler `warnings: []`, `pending_uploads: []`. The firmware block was inserted immediately after its instructions in the final flat array.
- Final assembled-content editorial review completed separately from format validation: name/excerpt preserved and audience stated; two introductory paragraphs; six action steps; full-skill installation and refresh rule explicit; enclosure alternatives clear; claims traced to implementation and measured hardware results; experimental checks visible next to relevant use cases and in limits.
- Structure: 30 total blocks, six `step` blocks, one `flash-firmware` tool, two BOM rows, zero images. All tool settings are nested. `firmware_size` remains a pending string.
- Validator command:

  ```sh
  python3 /opt/homebrew/lib/node_modules/@seeed-studio/sensecraft-cli/skills/sensecraft-cli/create-app/assets/validate_manifest.py codecraft-app.home-eyes.json --scene-ids 1,2,3,4,5,6,7,8,999
  ```

- Validator result: **PASS**, exit 0, **three expected warnings**: coverless private draft, pending firmware URL, and the same URL reported as empty. Format validation is complete; catalog identity uses reviewer-confirmed evidence; deployment inputs and release-device acceptance remain pending.
- Additional local checks passed: complete six-step journey, unique block IDs, exact Watcher config and enclosure SKUs, adjacent flash instructions/tool/replug sequence, full-skill installation/refresh wording, corresponding local GitHub link targets, credential-pattern and whitespace checks, and `git diff --check`. Five unique external links were inspected against local evidence; remote accessibility remains pending. Final artifact discovery still found zero matching release directories.
- Changes remain in the working tree for review. Remote identity and verified sync metadata retain their fresh-draft values.
