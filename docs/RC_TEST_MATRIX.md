# LA Studio release-candidate test matrix

Copy this file into the release evidence for each RC. Fill the result, tester, date, hardware, and evidence link for every applicable scenario. An unchecked scenario is not a pass; record it as `Not run` with an owner and target release.

## Release identity

| Field | Value |
|---|---|
| Version / tag | |
| Commit SHA | |
| Installer SHA-256 | |
| Test period | |
| Release owner | |

## Environments

| Environment | OS/build | CPU | GPU/driver | Result | Tester/evidence |
|---|---|---|---|---|---|
| Clean Windows 11 VM/Sandbox | | | None | | |
| NVIDIA CUDA machine | | | | | |
| AMD or Intel iGPU machine | | | | | |
| Pre-AVX2 CPU (if supported) | | | | | |

## Installation and lifecycle

| Scenario | Expected result | Result | Evidence / owner |
|---|---|---|---|
| Clean install without VS, VC++ redist, FFmpeg, or eSpeak NG | Installer finishes; app starts | | |
| Runtime host | Host launches, pings, and exits with no orphan process | | |
| Upgrade from previous stable | Models, history, settings, and projects survive | | |
| Downgrade to previous stable | One installed version remains and app starts cleanly | | |
| Uninstall keep-data option | App is removed; user data remains | | |
| Uninstall remove-data option | Behaviour matches the prompt and documentation | | |
| Native Windows frame | Windows 11 snap layouts, Aero Snap, shadow, minimize/maximize/restore, and edge resize work | | |
| Window placement restore | Normal and maximized placement restore predictably after restart, including on a secondary HiDPI monitor when available | | |

## Core workflows

| Scenario | Expected result | Result | Evidence / owner |
|---|---|---|---|
| Offline first run | Usable state; clear messages; no crash | | |
| Model/runtime download | Hash validation and extraction succeed | | |
| Speech-to-text | Representative WAV completes successfully | | |
| Text-to-speech | Synthesis and playback complete | | |
| Voice cloning | Reference-audio flow completes when supported | | |
| Video dubbing | Ingest, separation, transcription, translation, synthesis, mix, and export complete | | |
| Phoneme budgeting | eSpeak-backed budgeting is active | | |
| Local API | Unauthenticated request is 401; Bearer request works | | |

## Security and compliance

| Scenario | Expected result | Result | Evidence / owner |
|---|---|---|---|
| Artifact signature | `signtool verify /pa /v` succeeds for installer and both executables | | |
| Artifact hash | `Get-FileHash` matches published `SHA256SUMS` | | |
| Runtime integrity | Tampered archive is rejected | | |
| IPC privacy | Runtime token is absent from process command lines | | |
| API browser protection | Foreign Origin is rejected; query-string key is rejected | | |
| Diagnostics | Sanitized diagnostics omit text previews, tokens, and home prefixes | | |
| License payload | Notices and required license texts are present in the staged installer tree | | |

## Localization and performance

| Scenario | Expected result | Result | Evidence / owner |
|---|---|---|---|
| English UI | Primary studio views display correctly | | |
| Vietnamese UI | Primary studio views display correctly | | |
| Startup | Time to first usable window recorded | | |
| Memory | Peak memory for representative large-model workload recorded | | |

## Final sign-off

| Role | Name | Date | Decision / notes |
|---|---|---|---|
| Engineering | | | |
| QA / release owner | | | |
| Security / compliance reviewer | | | |

Promotion decision: **Approved / Blocked / Approved with explicitly accepted risks**
