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

For `stt`, `forced-alignment`, `voice-isolation`, and `voice-cloning`, the
worker entry must include `audio_path`.  Alignment also needs `transcript`;
voice cloning needs `reference_text`.  These are mandatory so the tool cannot
turn a health check into a false feature pass.  Voice cloning submits explicit
consent and removes the temporary profile after the test.

The report is acceptance evidence only if all three checks pass for a worker:

1. `/health` proves `ready=true`, `device=cuda`, no CPU fallback, and the
   selected model ID.
2. `/v1/capabilities` advertises that same capability/model as loaded on CUDA.
3. A real feature request returns a valid result: text, patches, monotonic
   timestamp segments, or a WAV artifact as appropriate.

`31/31` generated notebooks, unit-test counts, and an EXE startup smoke test
do not replace this evidence.
