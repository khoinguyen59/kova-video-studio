# Colab exact-model status — 0.0.1.1

**Recorded:** 2026-07-29
**Scope:** Direct Colab GPU routes. API Gateway remains independent and is not
used as a fallback for any route in this report.

## What this report proves

The source, checked-in notebooks, UI route surfaces and desktop runner
contracts were checked after the `0.0.1.1` STT asynchronous-job change. Every
route below has an exact selected-model ID, an exact notebook mapping, an
authenticated CUDA-only preflight, and a runner contract that sends that model
ID to the direct worker.

This is **not** evidence that all hosted models have completed an inference on
a user-owned Colab GPU. That last acceptance step requires a live GPU runtime,
the temporary URL/token printed by each notebook, and a safe sample for each
model.

## Feature-by-feature source verification

| Feature | Exact notebooks/models | Current automated evidence | State |
| --- | ---: | --- | --- |
| Speech to Text | 4 | `TestSttSession` 15/15; long recordings use `POST /v2/jobs/transcriptions` then short status polls | Source contract verified; live GPU required for each model |
| Text to Speech | 8 | `TestColabTtsRunner` 5/5; model/notebook mapping and direct speech request | Source contract verified; live GPU required for each model |
| Voice Cloning | 6 | `TestColabVoiceCloneRunner` 5/5; consent, profile/generation job model and notebook mapping | Source contract verified; live GPU required for each model |
| Voice Design | 3 | `TestColabVoiceDesignRunner` 4/4; model-bound direct request and mapping | Source contract verified; live GPU required for each model |
| Forced Alignment | 4 | `TestColabAlignmentRunner` 5/5; exact model request, cancellation and timing validation | Source contract verified; live GPU required for each model |
| Voice Isolation | 2 | `TestColabSeparationRunner` 5/5; job, artifact and cancellation contract | Source contract verified; live GPU required for both models |
| Translation | 3 | `TestColabTranslationRunner` 5/5; direct batch model request and cancellation | Source contract verified; live GPU required for each model |
| LLM Chat | 1 | `TestColabChatRunner` 5/5; direct streaming model request and cancellation | Source contract verified; live GPU required |
| Video Dubbing | 27 cross-node routes | `TestDubbingProject` 47 pass, 5 skip only because unit-test environment lacks staged eSpeak; exact notebook mapping and non-fallback routes pass | Source workflow verified; one live source-to-export run required |

## Cross-cutting gates

| Gate | Result | What it rejects or proves |
| --- | --- | --- |
| `TestRemoteExecution` | 31/31 pass | Wrong model, wrong capability, CPU worker, unloaded model, stale verification result, credential scope leakage and implicit route fallback |
| `verify_generated_colab_notebooks.py` | 31/31 generated notebooks match source | A generator edit cannot leave a stale checked-in notebook |
| `verify_remote_feature_surface.ps1` | 8/8 direct Colab routes | Visible notebook plus URL/token controls, ungated setup surface, CUDA guard, capability and correct endpoint |
| `lint_qml.ps1` | pass | Current QML parses cleanly |
| `test_RemoteLivePreflightContract.ps1` | pass | Gateway and all eight Colab capability groups use isolated bearer tokens; a wrong exact model is rejected |

The app also rejects a worker before activation unless `/health` and
`/v1/capabilities` prove `ready=true`, CUDA, no CPU fallback, the selected
capability, and a loaded CUDA model entry with the exact selected ID.

## STT long-media correction

The `0.0.1.1` desktop package uses the new asynchronous STT worker contract:

1. The desktop uploads mono 16 kHz WAV with the selected model ID to
   `POST /v2/jobs/transcriptions`.
2. The Colab notebook returns `202 Accepted` and a job ID without waiting for
   GPU inference.
3. The desktop polls `GET /v2/jobs/transcriptions/{job_id}` until the exact
   model finishes, fails, or is cancelled.

This removes the former synchronous GPU request that Cloudflare could cut at
its 120-second proxy response limit. The old `/v1/audio/transcriptions`
endpoint remains in notebooks solely for earlier desktop versions; `0.0.1.1`
does not use it for direct Colab STT.

## Live acceptance still required

For every model the user intends to use:

1. Open that exact model's notebook from the feature UI, choose a Colab GPU
   runtime, run all cells, and copy its fresh URL/token.
2. Confirm the panel reports verified CUDA and the selected model, then run a
   representative request. Check cancellation and a disconnected/expired
   worker recovery where the feature supports them.
3. For Gateway-capable STT, TTS and Translation, repeat with Colab disconnected
   to confirm the independent Gateway route works without changing Colab
   credentials.
4. After Isolation, STT, Translation and TTS/Clone each pass live separately,
   run one complete Dubbing source-to-export workflow.

The public GitHub and Colab URL for the Whisper notebook are now served from
the repository's `main` branch. The historical `0.0.1.2` verification checked
every checked-in notebook URL: **38/38 returned HTTP 200**. Exact
file-to-model mappings remain covered by the tests above.
