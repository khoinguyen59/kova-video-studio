# STT Colab model-selection audit — LA Studio 0.0.0.9

Date: 2026-07-29
Scope: Speech-to-Text model picker, direct Colab GPU route, notebook contract, and local CPU route

## Result

The 0.0.0.8 flow did **not** guarantee that Colab ran the model selected in the
four-card STT picker:

- every card opened the same `LA_STUDIO_SPEECH_GPU.ipynb`;
- that notebook always loaded Faster-Whisper `large-v3`;
- the app sent the generic multipart value `model=colab`;
- the model dialog exposed only local file/runtime download actions.

Version 0.0.0.9 replaces that ambiguous flow with an explicit one-model worker
contract. A selected family ID is carried from QML to `SttSessionController`,
through `ColabSttRequest` and `ColabWorkerClient`, and into the multipart
`model` field. Each notebook loads one model only and returns HTTP 409 if the
request asks for a different model.

## Model-to-notebook mapping

| Model shown in STT | App/worker model ID | GPU implementation | Notebook |
|---|---|---|---|
| Nemotron-3.5 ASR Streaming 0.6B | `nemotron-3.5-asr-streaming-0.6b` | NVIDIA checkpoint through Transformers `AutoModelForRNNT`, CUDA FP16 | `LA_STUDIO_STT_NEMOTRON_3_5_0_6B_GPU.ipynb` |
| Whisper.cpp | `whisper.cpp` | Whisper `large-v3` through Faster-Whisper, CUDA FP16 | `LA_STUDIO_STT_WHISPER_GPU.ipynb` |
| Qwen3-ASR 0.6B | `qwen3-asr-0.6b` | `Qwen/Qwen3-ASR-0.6B` through `qwen-asr`, CUDA BF16 | `LA_STUDIO_STT_QWEN3_ASR_0_6B_GPU.ipynb` |
| Qwen3-ASR 1.7B | `qwen3-asr-1.7b` | `Qwen/Qwen3-ASR-1.7B` through `qwen-asr`, CUDA BF16 | `LA_STUDIO_STT_QWEN3_ASR_1_7B_GPU.ipynb` |

The `whisper.cpp` label identifies the LA Studio model family. Its Colab GPU
worker deliberately uses Faster-Whisper instead of downloading or executing a
Windows `whisper.cpp` runtime inside Colab.

## UI and execution behavior

- Every STT card now shows **Select for Colab** and
  **Select + open notebook**.
- Colab selection does not download or load any model on the Windows PC.
- The local path is labeled **local CPU** and GPU/CUDA/Vulkan runtime choices
  are removed from this STT picker surface.
- The STT settings panel shows the exact selected Colab model and derives the
  notebook link from that model.
- Connecting a worker is disabled until a Colab model has been selected.
- Colab and API Gateway remain independent. Selecting or connecting Colab does
  not modify the Gateway URL, key, model, or provider route.
- Transcription history records the exact Colab model ID.

## Notebook worker contract

All four workers:

- require a Colab CUDA runtime and have no CPU fallback;
- load the model before creating the public tunnel;
- advertise contract version 1 and exactly one loaded STT model;
- expose `/health`, `/v1/capabilities`, and
  `/v1/audio/transcriptions`;
- require a random bearer token for capability and inference requests;
- reject a mismatched model ID with HTTP 409;
- limit uploads to 512 MiB and audio duration to 30 minutes;
- serialize inference to one GPU request and return HTTP 429 when busy;
- print the exact model ID, tunnel URL, and temporary token.

## GitHub visibility and public access

This archived `0.0.0.9` audit predates the current public-test setup. Current
Colab buttons point to the public `main` branch of
`khoinguyen59/kova-video-studio`, so every selected model opens the same
notebook revision that is shipped by the current source.

Consequences:

- the direct Colab link can be opened anonymously while the repository remains
  temporarily public for live GPU testing;
- once a notebook is running, the generated `trycloudflare.com` worker URL is
  publicly routable for that session, but its endpoints require the random
  bearer token;
- the repository must return to private visibility after the reviewed live
  acceptance run.

This temporary public state was explicitly authorized for testing; do not use
it as authorization to distribute a release package publicly.

## Verification completed

- Notebook JSON and every Python code cell parse successfully.
- The remote feature surface gate passes for 8/8 direct Colab routes.
- Focused STT unit suite passes 15/15.
- Multipart integration test verifies the exact selected model ID is posted.
- Static notebook tests verify all four family IDs, upstream models, CUDA
  guard, bearer token, endpoint, mismatch guard, limits, and tunnel output.
- QML lint passes.
- Headless QML route smoke passes, including changing through all STT cards
  without committing a local model selection.
- The full LA Studio unit-test executable completes with status 0.

## Live validation still required

No test on this Windows machine can prove that Google has allocated a live
Colab GPU or that current Colab package installation and model download
complete successfully. That is not treated as a passed test.

Validate one model at a time:

1. In **Load Model**, select the desired STT card.
2. Click **Select + open notebook**.
3. In Colab, authorize the private GitHub repository, choose a GPU runtime, and
   run all cells.
4. Copy the printed URL and token into STT settings.
5. Connect, upload a short known audio sample, and transcribe.
6. Confirm `/v1/capabilities` and the transcription response report the same
   model ID shown in LA Studio.
7. Repeat for the next model only after the current model passes.

This live GPU pass is the remaining evidence required before declaring all four
Colab models operational.
