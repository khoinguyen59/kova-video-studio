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

Each capability has its own in-memory Colab session. Pairing a TTS, alignment,
translation, or voice worker never replaces the URL/token paired for another
capability; this is necessary because their notebooks may run as distinct
temporary Colab workers. The GPU & Optional API settings catalog aggregates the
models advertised by every active worker, preserving the capability and worker
source of each entry; it never combines their tokens or routes.

| Capability | Notebook | Direct worker contract |
| --- | --- | --- |
| Speech-to-Text | `LA_STUDIO_SPEECH_GPU.ipynb` | `/v1/audio/transcriptions` |
| Text-to-Speech | `LA_STUDIO_VOICE_GPU.ipynb` | `/v1/audio/speech` |
| Voice Cloning | `LA_STUDIO_VOICE_CLONE_GPU.ipynb` | voice profile and generation jobs |
| Voice Design | `LA_STUDIO_VOICE_DESIGN_GPU.ipynb` | voice design jobs |
| Voice Isolation | `LA_STUDIO_SEPARATION_GPU.ipynb` | separation jobs |
| Forced Alignment | `LA_STUDIO_ALIGNMENT_GPU.ipynb` | alignment jobs |
| Translation | `LA_STUDIO_LANGUAGE_GPU.ipynb` | `/v1/translations` |
| LLM Chat | `LA_STUDIO_LANGUAGE_GPU.ipynb` | `/v1/chat/completions` |

Every notebook exposes `/health` and `/v1/capabilities` in addition to its
feature endpoint. The capabilities response identifies CUDA-backed models that
the active worker can serve and declares `contract_version: 1`; an older or
different contract is rejected before model selection. The notebooks contain no
Gateway URL, API key, or Gateway forwarding logic.

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
own bearer token. It requires each Gateway catalog and expected Colab capability
to expose at least one model ID, plus `ready=true` and `device=cuda` for Colab.
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
switching provider.

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
