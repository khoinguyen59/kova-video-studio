# AI agent response - Dubbing Direct Colab code verification

Date: 2026-08-05

## Code-path finding

This audit does not use a user screenshot, infer a running EXE version, or
claim a live-worker outcome. It traces the source and its loopback protocol
tests. A selected Direct Colab Spleeter route is not allowed to execute Local
source separation:

- `DubbingJobRunner::startSourceSeparation()` sends a verified
  `colab-direct` selection only to `ColabSeparationRunner`, then returns.
  The Local separation branch is below that return and cannot run for the same
  request.
- The archived 0.0.2.27 package has an older inline `sherpa_onnx` CUDA worker,
  while 0.0.2.28 ships a different bounded ONNX Runtime CUDA worker. This is
  a package comparison only; it is not an assertion about the user's session.

No app source was changed and no package was produced in this verification
task.

## Verified replacement

Use the already-audited package:

`out/LA-Studio-0.0.2.28/LA-Studio-0.0.2.28.exe`

- FileVersion/ProductVersion: `0.0.2.28` / `0.0.2.28`
- SHA-256: `63BA1B5B36A70039ADAB92FA5DB607E0556A3C5A7A55B515B116B516D02A4D92`
- Its notebook pins worker commit
  `f1b26005b6e3677db444ac12774ba3eaf9d9b204`, verifies both worker-template
  hashes, requires a CUDA startup probe, uses
  `cudnn_conv_algo_search=DEFAULT`, and processes long audio in 20-second
  cores with 1.5-second context. It reports `cpu_fallback: false`.

For a future manual acceptance run, use a fresh worker created by the 0.0.2.28
notebook, wait for `Exact CUDA worker passed startup probe`, then paste its URL
and token into Colab setup and press Check Colab. That is a manual procedure,
not an outcome claimed by this report.

## Verification executed

- Targeted Dubbing/Direct-Colab/remote/QML suites: **8/8 PASS** in
  **28.85 seconds**.
- Full CTest: **39/39 PASS** in **72.18 seconds**.
- Inspected both packaged notebook generations: 0.0.2.27 contains the old
  inline `sherpa_onnx` worker; 0.0.2.28 contains the audited bounded ONNX
  Runtime CUDA worker and launcher in `docs/colab-notebooks/workers`.

These are controller, protocol, notebook-contract and offscreen-QML tests.
They do not replace a live Colab run, which requires the fresh 0.0.2.28
notebook/session described above.
