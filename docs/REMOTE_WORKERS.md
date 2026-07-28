# Remote Inference Workers

LA Studio runs heavy inference through one of two independent routes:

- **API Gateway** uses only the configured Gateway URL and Gateway API key.
- **Direct Colab GPU** uses only the temporary worker URL and bearer token printed by a notebook.

The application never forwards a request from one route to the other, copies
credentials between them, or treats a failure on one route as permission to use
the other. Selecting a route changes only the route for the current feature.
Disconnecting a route does not alter the other route's configuration or session.

## Remote-first mode

Remote-first mode is enabled by default. It prevents automatic local model
downloads and local inference for the supported heavy capabilities. Local Dev is
available only after explicitly disabling Remote-first mode, for development or
offline comparison.

## API Gateway

Enter the Gateway URL and API key in Settings. The app obtains the Gateway model
catalog from `GET /v1/models` and sends requests only to the selected Gateway
endpoint. A Gateway connection is sufficient on its own; no Colab worker is
required.

Translation batches use the Gateway chat-completions endpoint with a strict JSON
`patches` contract. The desktop rejects prose, missing IDs, duplicate IDs, and
unknown IDs before it changes a translation project.

## Direct Colab GPU

Open the notebook for the capability, choose a GPU runtime in Colab, and run all
cells. Each notebook rejects CPU fallback, creates a fresh bearer token, and
prints a temporary HTTPS URL and token. Pair those values in the corresponding
studio settings. Colab session values stay in memory and must be paired again
after a notebook or Colab runtime reset.

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
the active worker can serve. The notebooks contain no Gateway URL, API key, or
Gateway forwarding logic.

## Selecting a route

For each capability, choose either **API Gateway** or **Colab GPU** in that
studio. Both can be configured at the same time, but only the selected route is
used for a request. For example, in Speech-to-Text a paired Colab worker remains
paired when Gateway is selected; turning off Gateway leaves no selected remote
route until the user explicitly selects Colab again.

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
