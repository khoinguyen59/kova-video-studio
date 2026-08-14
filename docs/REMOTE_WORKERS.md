# Remote Inference Workers

LA Studio runs heavy inference through one of two independent routes:

- **API Gateway** uses only the configured Gateway URL and Gateway API key.
- **Direct Colab GPU** uses only the temporary worker URL and bearer token printed by a notebook.

The application never forwards a request from one route to the other, copies
credentials between them, or treats a failure on one route as permission to use
the other. Selecting a route changes only the route for the current feature.
Disconnecting a route does not alter the other route's configuration or session.

## Local CPU and direct Colab GPU

LA Studio starts without a remote route. Installed local models run on the
computer's CPU, and no API URL, API key, or Colab token is needed to open the
application or use those local capabilities. Local GPU offload is disabled.

When a feature requires GPU acceleration, open that feature's own settings,
run its matching Colab notebook, then paste the temporary worker URL and bearer
token into the Colab fields shown there. Connecting a worker selects direct
Colab GPU only for that feature; it does not change any other feature's route.
The general GPU & Optional API page is an overview of already-connected workers,
not the place where workers are paired.

## API Gateway

Enter the Gateway URL and API key in Settings. The app obtains the Gateway LLM,
STT, and TTS catalogs from `GET /v1/models`, `GET /v1/models/stt`, and
`GET /v1/models/tts`, respectively, and sends requests only to the selected
Gateway endpoint. A Gateway connection is sufficient on its own; no Colab worker
is required.

Translation batches use the Gateway chat-completions endpoint with a strict JSON
`patches` contract. The desktop rejects prose, missing IDs, duplicate IDs, and
unknown IDs before it changes a translation project.

## Direct Colab GPU

Open the notebook for the capability, choose a GPU runtime in Colab, and run all
cells. Each notebook rejects CPU fallback, creates a fresh bearer token, and
prints a temporary HTTPS URL and token. Pair those values in the corresponding
studio settings. Colab session values stay in memory and must be paired again
after a notebook or Colab runtime reset.

The app does not treat a pasted URL/token as connected immediately. It shows
**Checking** while it calls that worker's `/health` and `/v1/capabilities` with
the token, and only enables the feature after it verifies `ready=true`, CUDA,
the selected capability and the exact selected model. A CPU worker, expired
tunnel, wrong notebook/model, or incompatible capability remains inactive and
shows an error beside the feature's URL/token fields.

Each capability has its own in-memory Colab session. Pairing a TTS, alignment,
translation, or voice worker never replaces the URL/token paired for another
capability; this is necessary because their notebooks may run as distinct
temporary Colab workers. The GPU & Optional API settings catalog aggregates the
models advertised by every active worker, preserving the capability and worker
source of each entry; it never combines their tokens or routes.

| Capability | Notebook | Direct worker contract |
| --- | --- | --- |
| Speech-to-Text | One exact-model notebook selected from the four `LA_STUDIO_STT_*_GPU.ipynb` workers | model-bound asynchronous `/v2/jobs/transcriptions`; the worker retains `/v1/audio/transcriptions` only for older desktop builds |
| Text-to-Speech | One exact-model notebook selected from the eight `LA_STUDIO_TTS_*_GPU.ipynb` workers | model-bound `/v1/audio/speech` |
| Voice Cloning | One exact-model `LA_STUDIO_VOICE_CLONE_*_GPU.ipynb` notebook selected from the model gallery | model-bound profile and generation jobs |
| Voice Design | One exact-model `LA_STUDIO_VOICE_DESIGN_*_GPU.ipynb` notebook selected from the model gallery | model-bound voice design jobs |
| Voice Isolation | Exact-model notebook selected from `LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb` or `LA_STUDIO_SEPARATION_UVR_VOCALS_GPU.ipynb`; its ONNX assets come from the public `k2-fsa/sherpa-onnx` GitHub Release declared as `artifact_url` | model-bound separation jobs |
| Forced Alignment | Exact-model notebook selected from the four `LA_STUDIO_ALIGNMENT_*_GPU.ipynb` workers | model-bound alignment jobs |
| Translation | One exact-model notebook selected from the three `LA_STUDIO_TRANSLATION_*_GPU.ipynb` workers | model-bound `/v1/translations` |
| LLM Chat | `LA_STUDIO_LLM_QWEN3_5_2B_GPU.ipynb` | model-bound `/v1/chat/completions` |

Every notebook exposes `/health` and `/v1/capabilities` in addition to its
feature endpoint. The capabilities response identifies CUDA-backed models that
the active worker can serve and declares `contract_version: 1`; an older or
different contract is rejected before model selection. The notebooks contain no
Gateway URL, API key, or Gateway forwarding logic.

## Public media download

**Download media** is a local CPU-only utility, not a Colab worker and not an
API Gateway route. It uses LA Studio's SHA-pinned `yt-dlp` runtime to resolve
public HTTPS pages. A user may optionally provide a Netscape cookie export for
one retry; LA Studio never reads a browser cookie store. The completed file is
then selected explicitly before it enters a Dubbing project.

If a user independently created media in a Colab notebook, download the exact
output printed by that notebook's final cell from the **Files** folder in
Colab's left sidebar, then use **Choose local files**. No worker URL or token
belongs on the download page.

## Live preflight, one route at a time

Before a feature-specific live smoke test, validate the selected route without
uploading media or generating any output. Copy
`docs/examples/remote-live-preflight.example.json` outside the repository,
replace only its `baseUrl` values, and set the named secrets in the current
PowerShell session. Do not put a token or API key in the JSON file.

```powershell
$env:LA_STUDIO_GATEWAY_API_KEY = '...'
$env:LA_STUDIO_COLAB_STT_TOKEN = '...'
.\scripts\smoke_remote_preflight.ps1 -ConfigPath C:\secure\remote-live.json -Only gateway
.\scripts\smoke_remote_preflight.ps1 -ConfigPath C:\secure\remote-live.json -Only stt
```

The runner checks all three Gateway catalogs with the Gateway key, and the
selected Colab worker's `/health` plus `/v1/capabilities` with that worker's
own bearer token. Each Colab entry must name `expectedModel`; the runner
requires that exact ID from both endpoints, with `ready=true`, `device=cuda`,
`cpu_fallback=false`, and a loaded CUDA model entry. It does not accept merely
any CUDA model for the requested capability.
It records those advertised model IDs in a redacted report below `out/`; it never writes tokens,
headers, raw API responses, URL paths, or query strings. Use `-DryRun` first to
validate the configuration without reading credentials or making requests.

This is deliberately only a connectivity/CUDA/model preflight. Run inference
smoke separately for each selected capability; voice cloning additionally
requires a consented reference recording and must not use an unconsented sample.

## Selecting a route

For a GPU capability, use **Colab GPU** in that studio. The Worker URL and
Session token fields are immediately next to the GPU configuration so the model
and its execution location are set together. API Gateway remains an optional,
independent route for a later deployment; leaving its fields empty never blocks
local CPU or direct Colab GPU work.

Video Dubbing composes the same feature controllers. Each stage uses its chosen
provider directly, and a failed stage reports that provider's error without
switching provider. Dubbing shows the exact model before a worker is paired and
opens that model's notebook. Voice cloning and optional forced alignment have
their own temporary sessions inside Dubbing; they never reuse or overwrite the
TTS or STT session. See `docs/DUBBING_REMOTE_EXECUTION_AUDIT.md`.

## Security and troubleshooting

- Keep Gateway API keys in the Settings secure store; do not put them in a
  notebook or source file.
- Treat a Colab URL and bearer token as temporary credentials. Do not share them
  in logs, screenshots, or project files.
- Re-run the relevant notebook and pair again after a Colab reset or tunnel
  expiry.
- A failed Gateway request does not require restarting Colab. A failed Colab
  request does not change the Gateway configuration.
- Check the worker's `/v1/capabilities` response before selecting a Colab model.
