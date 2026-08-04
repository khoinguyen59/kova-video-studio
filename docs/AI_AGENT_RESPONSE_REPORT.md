# AI agent response - 0.0.2.25 packaged

Date: 2026-08-05

## Outcome

The reported Automatic Dubbing error is fixed at the provider boundary. The
wizard had already verified Direct Colab, but automatic setup still followed
the unrelated global `remoteFirstMode` preference and tried to download a local
Sherpa runtime. The downloader correctly rejected that local runtime because
its catalog entry has no SHA-256. It was not a reason to bypass checksum
verification or to fall back to Local.

Automatic setup now reads the saved provider for each model node. A verified
exact Direct Colab capability/model stays Direct Colab; a configured API
Gateway stays API Gateway; neither can schedule a hidden local download or
load. Remote TTS also remains remote. Local continues to be an explicit route
and its checksum gate is unchanged.

The Activity row now reports the actual user stage rather than the generic
`Running dubbing workflow` / `model-setup` state. For example it shows
`Dubbing - Isolator`, `Isolator (3/8)`, `Direct Colab GPU` and the selected
model. Numeric progress is shown only for a real worker/download counter. A
health check or worker that has not provided a counter says `Working`; this
avoids fabricated fixed 5%/8% progress.

## Validation

- New controller/Activity regression starts four verified exact-model Direct
  Colab mock workers with `remoteFirstMode=false`. It proves that Automatic
  leaves setup without a local Sherpa download and that Activity maps the
  current node to `Isolator (3/8)` on Direct Colab.
- Targeted Dubbing, Direct Colab and QML tests: **7/7 PASS**.
- QML lint: PASS.
- Full CTest: **39/39 PASS** in 36.02 seconds.
- Packaged offscreen QML smoke: PASS, exit code 0 and 18 ordered workflow
  interaction events.
- Graphify code graph was updated after source changes. It emitted known parser
  warnings for unrelated files but completed the code graph update.

## Package audit

- EXE: `out/LA-Studio-0.0.2.25/LA-Studio-0.0.2.25.exe`
- Source/FileVersion/ProductVersion: `0.0.2.25` / `0.0.2.25` / `0.0.2.25`
- SHA-256: `78F1671A5810EEF6D519A4E1A97F17E467F234DCF54EE6917ED111FDF4E579B7`
- Verified: Qt Windows and offscreen platform plugins, Qt Multimedia,
  FFmpeg/FFprobe, RuntimeHost, Tesseract/Paddle runtimes and staging/license
  manifests (19 required artifacts).
- Internal-only caveat: the eSpeak MSI is SHA-verified but unsigned; this is
  not a distributable release.

Source/tests are committed and pushed directly to `main` as `0038c28`
(`fix: preserve dubbing routes and stage activity`). No visible GUI or live
Colab worker was opened. The package smoke is offscreen only; manual desktop
interaction and a real user Colab session remain separate acceptance gates.
