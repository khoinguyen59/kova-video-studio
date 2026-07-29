# STT Colab upload reliability — 0.0.1.3

## Incident evidence

The Dubbing Transcribe node failed before a Colab job was created. The local
log recorded a decoded 14,397,456-sample audio input and then, within seconds,
reported only `Colab worker returned HTTP 500`. The previous desktop client
sent the complete generated WAV as one multipart request to
`/v2/jobs/transcriptions`; it discarded a non-JSON proxy/worker error body.

The evidence did not identify one model-specific inference failure: the worker
never returned a job id. Therefore this release removes the fragile long
multipart upload path for current notebooks rather than treating 500 as a
retryable success or pretending that transcription started.

## Changes

1. The desktop STT client now creates an authenticated upload session and
   sends the normalised WAV in acknowledged 2 MiB chunks. It commits the job
   only after the worker confirms every byte. Older notebooks explicitly
   returning `404` or `405` retain the old multipart contract for backwards
   compatibility.
2. Each exact-model STT notebook exposes the chunk upload endpoints, enforces
   size/order/completeness, cleans abandoned uploads, validates the input, and
   only then claims the single GPU slot. Unhandled worker exceptions now return
   a bounded diagnostic and are printed in the Colab cell output.
3. `Check connection` is available beside every visible `ColabSessionStatus`.
   It reruns `/health` and `/v1/capabilities` for the active exact
   capability/model and visibly reports checking, success, or failure. It
   never exposes or persists the token.
4. Dubbing STT progress is event-derived: 1% request accepted, 2% audio
   decode dispatched, 3% decode complete, 4% WAV construction, 5–20%
   confirmed upload bytes, then 20–95% worker-reported job state. The desktop
   no longer assigns an unexplained initial 5%.
5. `package.ps1` now rejects a CMake build cache whose compiler probe still
   names a non-MSVC archiver, then rebuilds that generated cache with both
   `link.exe` and `lib.exe`. This prevents a MinGW `ar.exe` on PATH from
   producing an invalid MSVC release package.

## Verification

- All four generated STT notebooks passed Python syntax and contract checks.
- `LAStudioUnitTests.exe` passed the chunked STT contract, notebook contract,
  Colab recheck, and Colab status UI tests.
- A clean MSVC package build completed and produced a portable
  `LA-Studio-0.0.1.3.exe` with Qt, FFmpeg/FFprobe, eSpeak, libcurl, zlib, and
  the Windows platform plugin staged beside it.

## Required live acceptance

This package works with the new notebooks. For a live run, open the notebook
for the exact model selected in the app from the updated repository, run all
cells, paste its new tunnel URL and token, and press `Check connection` before
starting transcription. A notebook session from before this release will use
the compatibility multipart route and cannot exercise the new chunk protocol.
