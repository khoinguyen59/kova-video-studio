# AI agent response - 0.0.2.27 packaged

Date: 2026-08-05

## Outcome

The reported Automatic Dubbing errors were route-integrity failures, not a
reason to bypass the secure checksum or disk-space checks. Adaptive quality
could still start a hidden Local Qwen setup, and an older Local runtime
selection could survive inside nested node parameters after the visible route
had changed to Direct Colab.

Adaptive rewrite now has an explicit Direct Colab route for exact
`qwen3.5-2b`. It opens the packaged notebook
`LA_STUDIO_LLM_QWEN3_5_2B_GPU.ipynb`, requires a verified
`llm-chat/qwen3.5-2b` CUDA worker, and appears under the existing Translate
stage as a subordinate worker. If it has not been connected and checked,
preflight stops with a setup action; it never downloads or falls back to a
Local Adaptive LLM.

For every API Gateway or Direct Colab selection/reselection, Local-only model
metadata is now removed at both the project root and nested parameters. The
remote route/model persists across reopen. Direct Colab keeps its URL/token in
memory only and no longer clears a separately configured API Gateway URL or
credential. Selecting the Dubbing adaptive worker also does not change the
standalone LLM Chat controller.

## Validation

- Controller regression covers a legacy Local -> Direct Colab project, repeat
  Direct selection with stale nested runtime fields, exact verified CUDA
  workers for separation/STT/translation/TTS/LLM, persistence and secret
  exclusion, the Translate subordinate worker card, and no Local Adaptive
  download.
- QML lint: PASS.
- Targeted Dubbing/remote/Colab/QML tests: **7/7 PASS**.
- Full CTest: **39/39 PASS** in 72.18 seconds.
- Graphify code graph updated after source changes. It completed with known
  unrelated parser warnings; graph files were not committed.

## Package audit

- EXE: `out/LA-Studio-0.0.2.27/LA-Studio-0.0.2.27.exe`
- Source/FileVersion/ProductVersion: `0.0.2.27` / `0.0.2.27` / `0.0.2.27`
- SHA-256: `9C18D49DB53DB14DC4D39CC7780F1923FDFAF283156552FBC03F57BE1FAC32B9`
- Verified: Qt Windows/offscreen plugins, FFmpeg/FFprobe, RuntimeHost,
  Subtitle OCR/Paddle manifests, LGPL license, and
  `LA_STUDIO_LLM_QWEN3_5_2B_GPU.ipynb`.
- Internal-only caveat: the eSpeak MSI is SHA-verified but unsigned.

Candidate `0.0.2.26` was staged before the final nested-metadata regression
was found, so it is not accepted. Source/tests are committed directly to
`main` as `9b0eb6f` (`fix: keep adaptive dubbing routes remote`). No visible
GUI or live Colab worker was opened; manual desktop interaction and a real
user Colab session remain separate acceptance gates.
