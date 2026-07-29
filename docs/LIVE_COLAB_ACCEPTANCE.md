# Live Colab acceptance

`scripts/run_live_colab_acceptance.py` is the required evidence gate for an
exact Colab model.  It is not a mock test: it calls a currently running Colab
worker through its HTTPS tunnel, checks CUDA and the exact model ID, then
performs a small inference and validates the returned artifact.

It deliberately does not create or retain tokens.  Put the temporary URL and
token into **process environment variables**, not the JSON file.  The Markdown
report contains neither value.

Example configuration (save outside the repository, for example
`C:\Temp\la-studio-live-colab.json`):

```json
{
  "workers": [
    {
      "capability": "tts",
      "model": "kokoro",
      "url_env": "LASTUDIO_LIVE_TTS_URL",
      "token_env": "LASTUDIO_LIVE_TTS_TOKEN",
      "text": "LA Studio live validation.",
      "voice": "af_heart",
      "language": "en"
    },
    {
      "capability": "translation",
      "model": "m2m100-418m",
      "url_env": "LASTUDIO_LIVE_TRANSLATION_URL",
      "token_env": "LASTUDIO_LIVE_TRANSLATION_TOKEN",
      "source_language": "en",
      "target_language": "vi",
      "text": "LA Studio live validation."
    }
  ]
}
```

Set values only for the current terminal session, then run:

```powershell
$env:LASTUDIO_LIVE_TTS_URL = 'https://...trycloudflare.com'
$env:LASTUDIO_LIVE_TTS_TOKEN = 'temporary-token-from-colab'
python scripts/run_live_colab_acceptance.py `
  --config C:\Temp\la-studio-live-colab.json `
  --report out\live-colab-acceptance.md
```

## Exact-model template for all workers

For the complete model inventory, generate a secret-free template outside the
repository:

```powershell
python scripts/generate_live_colab_acceptance_template.py `
  --output C:\Temp\la-studio-live-colab.json
```

It contains one entry for each of the 31 exact notebook workers. Each entry
has its own URL/token environment-variable names, so a URL from one model
cannot accidentally be reused to accept another. Replace only the
`REPLACE_WITH_...` audio placeholders with short local test files; do not put
URLs or tokens in the JSON file. When one notebook is ready, run only its
entry, for example:

```powershell
$env:LASTUDIO_LIVE_TTS_KOKORO_URL = 'https://...trycloudflare.com'
$env:LASTUDIO_LIVE_TTS_KOKORO_TOKEN = 'temporary-token-from-colab'
python scripts/run_live_colab_acceptance.py `
  --config C:\Temp\la-studio-live-colab.json `
  --only tts:kokoro `
  --report out\live-colab-kokoro.md
```

The tracked [template](LIVE_COLAB_ACCEPTANCE_TEMPLATE.json) is generated from
notebook metadata and checked by `verify_generated_colab_notebooks.py`. A
model cannot be added, renamed, or removed without updating its live
acceptance entry.

The CI route gate also runs a localhost contract test of the acceptance runner
across all eight capability endpoint shapes. It proves the runner itself sends
the expected exact-model and artifact checks; it does **not** count as a live
Colab or model result.

For `stt`, `forced-alignment`, `voice-isolation`, and `voice-cloning`, the
worker entry must include `audio_path`.  Alignment also needs `transcript`;
voice cloning needs `reference_text`.  These are mandatory so the tool cannot
turn a health check into a false feature pass.  Voice cloning submits explicit
consent and removes the temporary profile after the test.

The report is acceptance evidence only if all four checks pass for a worker:

1. `/health` proves `ready=true`, `device=cuda`, no CPU fallback, and the
   selected model ID.
2. `/v1/capabilities` advertises that same capability/model as loaded on CUDA.
3. The live feature endpoint rejects a deliberately wrong model ID with HTTP
   `409`; a worker cannot silently ignore the selected model.
4. A real feature request returns a valid result: text, patches, monotonic
   timestamp segments, or a WAV artifact as appropriate.

`31/31` generated notebooks, unit-test counts, and an EXE startup smoke test
do not replace this evidence.
