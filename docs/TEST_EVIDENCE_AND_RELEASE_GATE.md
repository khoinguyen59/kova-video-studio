# Test evidence and release gate

## Correction to earlier pass-count reports

An earlier report such as `33/33 passed` or `34/34 passed` did **not** mean
that 33 or 34 end-user features had been run successfully.  It was a count of
registered CTest entries and test suites.  Those entries combine unit tests,
source/QML checks and HTTP loopback mocks.  They did not prove a packaged
desktop app, a live Colab notebook, or each model's actual inference result.

That wording must not be used as feature-acceptance evidence again.

## Evidence collected on the current source

| Scope | Result | What it proves | What it does not prove |
| --- | --- | --- | --- |
| `TestRemoteExecution` | 31 passed, 0 failed, 0 skipped | Endpoint validation, independent Gateway/Colab routing, CUDA/exact-model handshake parsing, and rejection of the wrong capability or model using isolated loopback workers | A public Colab tunnel, a running GPU or real model inference |
| `TestDubbingProject` | 49 passed, 0 failed, 5 skipped | Dubbing orchestration, remote-route guards and a loopback handshake proving that a worker verified for `tts/kokoro` is rejected before a `tts/vibevoice` inference request | A full desktop click-through, media export using real remote models, or live Colab GPU work |
| eSpeak-dependent Dubbing cases | 5 skipped | Nothing releasable | The eSpeak NG runtime is absent from this test environment. These must pass in a staged runtime before a release can be accepted. |

The Dubbing suite also exposed and fixed a test-harness teardown crash: a late
socket `disconnected` callback could access its mock's already-destroyed
request buffer.  A suite that crashes cannot be treated as green even if its
previous output contained only `PASS` lines.

`ctest -N` currently lists 35 registered entries in the repair build. That
folder was intentionally built only for `LAStudioUnitTests`, so its separately
registered `VietNormUnitTests.exe` has not been produced yet. Consequently a
full `ctest` result has **not** been claimed for this source state; the build
must be complete before that gate is run.

## Required gates, in order

1. **Compile gate**: build the changed application and test target without
   compiler/linker errors.
2. **Regression gate**: run named suites and report pass, fail and skip counts
   separately. A skip is not a pass.
3. **Desktop packaged-app gate**: install the generated package in a clean
   directory and exercise every affected UI flow. Verify the actual buttons,
   selected model, route fields, progress/cancel/error state and no UI freeze.
4. **Live model gate**: for every exact capability/model pair, start its real
   Colab notebook on a GPU and run
   `scripts/run_live_colab_acceptance.py`. It must verify health, CUDA,
   exact-model capability, wrong-model rejection and a real output artifact or
   inference response. Save the generated Markdown report with the commit.
5. **End-to-end feature gate**: run each product feature with its configured
   route. For Dubbing, this includes import, normalize, separation, STT,
   translation, voice, output and export; a stage cannot be marked accepted
   merely because its backend function has a unit test.

No EXE should be described as ready for release, and no new package should be
created, until the relevant gates above have recorded evidence. A real Colab
run requires an active user-owned notebook URL and token; without those it is
honest to mark the live gate **not run**, not passed.

## Prevention rule

Future status reports must use one of these labels:

- **Regression passed**: automated source/mock/loopback evidence only.
- **Packaged desktop verified**: a clean packaged-app UI run was completed.
- **Live Colab verified**: `run_live_colab_acceptance.py` completed for the
  named exact model and its report is available.
- **Feature accepted**: the complete user workflow has passed its desktop and
  live-model gates.

They must never be collapsed into a single `N/N passed` claim.
