# Translation and LLM Chat exact-model remote audit

**Source status:** implemented and locally verified.

**Live external status:** pending real Colab GPU sessions and a configured
9Router endpoint/key. Local HTTP tests prove the desktop contracts but do not
claim that external inference has run.

## Exact direct-Colab routes

| Feature | Gallery model ID | Notebook | Pinned upstream |
| --- | --- | --- | --- |
| Translation | `m2m100-418m` | `LA_STUDIO_TRANSLATION_M2M100_418M_GPU.ipynb` | `facebook/m2m100_418M` |
| Translation | `madlad400-3b-mt` | `LA_STUDIO_TRANSLATION_MADLAD400_3B_GPU.ipynb` | `google/madlad400-3b-mt` |
| Translation | `hy-mt2-1.8b` | `LA_STUDIO_TRANSLATION_HY_MT2_1_8B_GPU.ipynb` | `tencent/Hy-MT2-1.8B` |
| LLM Chat | `qwen3.5-2b` | `LA_STUDIO_LLM_QWEN3_5_2B_GPU.ipynb` | `Qwen/Qwen3.5-2B` |

The combined `LA_STUDIO_LANGUAGE_GPU.ipynb` remains as a legacy/Dubbing
compatibility worker. Translation Studio and LLM Chat Studio no longer open it.

## Research decisions

- M2M-100 uses its official encoder-decoder model and tokenizer with the
  requested source language plus the target language token.
- MADLAD uses its official T5-compatible model and `<2xx>` target prefix.
- Hy-MT2 follows Tencent's official Transformers loading path, chat template,
  translation-only prompt and published language names. Its notebook requires
  Transformers 5.6 or newer.
- Qwen3.5 follows the official `AutoProcessor` and
  `AutoModelForMultimodalLM` path. LA Studio exposes the existing text-chat
  capability only; the notebook does not add an unimplemented image UI.

Primary references:

- https://huggingface.co/facebook/m2m100_418M
- https://huggingface.co/google/madlad400-3b-mt
- https://huggingface.co/tencent/Hy-MT2-1.8B
- https://huggingface.co/Qwen/Qwen3.5-2B

Each notebook pins the current upstream model revision recorded during this
audit so an upstream branch update cannot silently change a worker.

## Desktop behavior

- Selecting a gallery model sets the exact direct-Colab model and opens its
  exact public GitHub notebook.
- Changing a Translation model cancels any active Colab translation, clears
  the old temporary URL/token and rejects a late response from the prior
  provider/model revision.
- Every Translation and Chat request carries the exact selected model ID.
- Chat now sends `context_tokens`, `top_k` and `repeat_penalty` to direct
  Colab. These controls were previously visible in the UI but absent from the
  Colab request payload.
- API Gateway remains separate: it uses the Gateway URL/key/model and never
  reads a Colab URL/token. Translation validates the strict JSON `patches`
  response before changing the project.

## Worker behavior

Every new notebook:

- loads its model on CUDA before reporting ready;
- advertises only its exact model in `/v1/capabilities`;
- rejects any mismatched request model with HTTP 409;
- refuses CPU fallback;
- generates a fresh bearer token and Cloudflare tunnel URL;
- contains no Gateway URL, API key, forwarding or fallback code.

## Verification performed

- All four notebook worker programs and all four startup programs parse as
  valid Python.
- The MSVC internal release target compiles successfully.
- `TestColabTranslationRunner`, `TestColabChatRunner`,
  `TestTranslationProject`, `TestLlmChatEngine`, `TestRemoteExecution` and
  `QmlRouteSmoke` pass.
- The direct Chat HTTP test verifies model, context size, top-k and repeat
  penalty in the actual JSON request.
- `scripts/verify_remote_feature_surface.ps1` passes 8/8 feature routes.

## Remaining live acceptance

Run each notebook on a real Colab GPU and verify the exact model from
`/health` and `/v1/capabilities`. Complete one translation for every
Translation model, then complete and cancel one real Qwen3.5 streaming chat.
Separately configure a 9Router endpoint/key and run one Chat response plus one
strict-JSON Translation response. Failure of either route must not configure
or invoke the other route.
