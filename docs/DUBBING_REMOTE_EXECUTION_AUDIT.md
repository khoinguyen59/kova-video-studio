# Dubbing remote-execution audit

Date: 2026-07-29
Source candidate: `main`
Packaging status: not packaged; live Colab inference is still required

## Result

The Dubbing source pipeline now keeps API Gateway and direct Colab execution
independent and binds every direct GPU request to one exact model notebook.
Source compilation, the Dubbing test suite, the complete desktop test suite,
the QML route smoke test, and the public GitHub notebook inventory all pass.

This is source-level acceptance, not proof that a real Colab GPU completed the
workflow. A live worker URL/token and non-sensitive media sample are still
required for the final source-to-export test.

## Per-node route matrix

| Dubbing stage | API Gateway | Direct Colab GPU | Local behavior |
| --- | --- | --- | --- |
| Voice isolation | Not exposed | 2 exact workers: Spleeter 2 Stems FP16, UVR Vocals FT | CPU local remains available |
| Speech to text | Independent Gateway STT route | 4 exact workers: Nemotron 3.5 ASR 0.6B, Whisper.cpp, Qwen3-ASR 0.6B, Qwen3-ASR 1.7B | CPU local remains available |
| Forced alignment after remote STT | Not exposed | Optional, separate worker and session; 4 exact alignment models | A remote STT run never loads the local aligner. If optional Colab alignment is disabled, the STT timestamps are retained |
| Translation | Independent Gateway translation route | 3 exact workers: M2M100 418M, MADLAD-400 3B MT, Hy-MT2 1.8B | CPU local remains available |
| Duration-aware LLM rewrite | Uses the shared Gateway LLM URL/key/model in remote-first mode | Not coupled to the translation Colab worker | Existing local/CLI choices remain available outside remote-first mode |
| Speech synthesis | Independent Gateway TTS route | 8 exact TTS workers | CPU local remains available |
| Automatic voice cloning | Deliberately rejected because no compatible Gateway clone adapter exists | Separate temporary worker and session; 6 exact clone models; explicit consent required | Existing local clone-capable runtime remains available |

Total Dubbing-specific exact routes: **27**.

## Defects corrected

1. The Dubbing UI opened the legacy generic speech, language, voice, and
   separation notebooks instead of the notebook for the selected model.
2. A user could not choose a Colab model before connecting because the picker
   depended on the active worker catalog, while the correct worker depended on
   that model choice.
3. Source separation silently defaulted to `htdemucs`, which none of the two
   exact workers serve.
4. TTS silently defaulted to `kokoro`/`af_heart` even when another worker was
   selected.
5. Automatic voice cloning used a separate session but exposed no Dubbing
   connection UI and sent an empty model ID. Exact-model workers therefore
   rejected every clone request.
6. After remote STT, the pipeline could invoke local forced alignment without
   showing that GPU/model dependency in Dubbing.
7. Remote-first duration rewriting retained an LM Studio localhost default
   instead of using the configured API Gateway LLM route.
8. Colab model changes were partially validated through the live worker
   catalog. A currently connected old worker could therefore prevent choosing
   the new model needed to open a new notebook.

## Current behavior

- Selecting `Colab GPU` chooses a supported default exact model and shows all
  supported models before a worker is connected.
- Changing the model invalidates the previous model-bound temporary session.
- The dialog displays the exact model and exact notebook; the model field is
  not free-form.
- The desktop rejects an unmapped model before uploading media.
- The worker also rejects a mismatched model with its exact-model contract.
- Voice cloning and forced alignment have separate model selectors,
  notebooks, URLs, tokens, status labels, and sessions inside Dubbing.
- Worker credentials remain memory-only and never enter API Gateway settings
  or the project file.
- API Gateway failures do not fall through to Colab or local inference.
- Colab failures do not fall through to API Gateway or local inference.

## Automated evidence

| Gate | Result |
| --- | --- |
| MSVC release compile | Pass |
| `TestDubbingProject` | 52/52 pass |
| Complete CTest suite | 34/34 pass |
| QML route smoke | Pass |
| Direct route surface script | 8/8 pass |
| Exact Dubbing route count | 27 |
| Exact notebook files present locally | 27/27 |
| Exact notebook files visible in public GitHub branch | 27/27 |
| Generic Dubbing notebook references removed | Pass |
| Dubbing clone request contains exact model | Pass |
| Remote STT avoids hidden local alignment | Pass |

The notebook inventory was checked against the public
`khoinguyen59/kova-video-studio` repository on branch `main`. The repository
remains temporarily public only for
the pending live Colab tests.

## Live acceptance still required

For each chosen model:

1. Open the exact GitHub-backed notebook from Dubbing.
2. Select a Colab GPU runtime and run all cells.
3. Confirm `/health` reports `ready=true`, `device=cuda`, and the exact model.
4. Paste only that worker's URL/token into the matching Dubbing node.
5. Run the node with non-sensitive test data.
6. Change to a different model and verify the old session is cleared.
7. Complete one end-to-end flow:
   isolation → STT → optional alignment → translation → optional rewrite →
   TTS or consented clone → timing → mix → export.
8. Confirm the UI remains responsive and that no local GPU process is started.

After live acceptance, return the GitHub repository to private visibility
before producing the final packaged candidate.
