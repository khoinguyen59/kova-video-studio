# AI agent response - Dubbing Direct Colab verification

Date: 2026-08-05

## Finding from the reported failure

The screenshot is from **LA Studio 0.0.2.27**, not 0.0.2.28.  Its
`CUDNN_FE_HEURISTIC_QUERY_FAILED` stack is emitted by the remote Spleeter
CUDA worker. It is **not** evidence that LA Studio selected a Local route:

- `DubbingJobRunner::startSourceSeparation()` sends a verified
  `colab-direct` selection only to `ColabSeparationRunner`, then returns.
  The Local separation branch is below that return and cannot run for the same
  request.
- The 0.0.2.27 notebook embedded the old `sherpa_onnx` CUDA wrapper, which
  sends the long decoded source through one heuristic cuDNN execution plan.
  This is the path that can produce the exact `CUDNN_FE_HEURISTIC_QUERY_FAILED`
  shown in the screenshot.

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

Do not reuse the old 0.0.2.27 Colab runtime or URL. Close it, start 0.0.2.28,
open the Spleeter notebook from that version, select a GPU runtime, Run all,
and continue only after it prints `Exact CUDA worker passed startup probe`.
Paste that newly printed URL and token into Colab setup, press Check Colab, and
then run Isolator.

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
