# AI agent response - Dubbing workspace and Subtitle OCR bootstrap

Date: 2026-08-11

## Delivered source change

Commits `d955dd9 fix: stabilize dubbing workspace and OCR bootstrap` and
`32ee731 fix: guard dubbing pane breakpoints` are pushed
directly to `origin/main`.

- Dubbing keeps the existing LA Studio capabilities, routes and workflow
  stages, but the header now reserves fixed space for **Generate**, **Colab**
  and the full **Workflow** action.  Only the stage rail scrolls; secondary
  actions are in an overflow menu instead of being clipped into labels such as
  `Wor` or reducing the video area.
- The Dubbing page keeps fixed layout participants: task controls on the left
  when explicitly opened, preview in the centre and task result/inspection on
  the right.  The central video preference is 940 px with a 440 px minimum for
  video.  The preview's editing buttons moved to their own horizontal toolbar,
  so they no longer overlap the video-state controls.  On constrained widths,
  History and the task shelf yield from layout rather than painting over the
  video.  The lower Timeline remains full width and has a 28 px drag target
  that tracks the pointer relative to the press position.
- The compact breakpoints are now calculated from the actual layout minima.
  Below 1450 px the left task shelf yields before History + task shelf +
  preview + review can exceed the workspace; below 1080 px History yields
  before it would make the compact Preview/Review pair unusable.  This prevents
  clipping at medium desktop sizes, not merely at a tiny-window threshold.
- Source/target language and quality remain in `DubbingProjectSetupDialog`,
  which follows the user's Automatic or Step-by-step choice.  The permanent
  `DubbingProjectStatusPanel` is not instantiated in `DubbingPage.qml`.
- `LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb` now identifies bootstrap
  revision `subtitle-ocr-bootstrap-2026-08-11.7`.  It removes only the legacy
  app-owned OCR bootstrap folder and uses one dedicated-target pip resolver
  transaction for the fixed Paddle GPU, PaddleOCR, PaddleX and Pillow stack.
  It does not call `venv.EnvBuilder`, `virtualenv` or `ensurepip`.

## Evidence

- Changed QML files parsed successfully with `qmllint`.
- Python generator and verifier compiled successfully.
- Generated exact-model notebooks verified: **32/32**.
- Independent source contract passed for the fixed Dubbing layout, popup-based
  project setup and single-transaction OCR bootstrap.
- `git diff --check` and `graphify update .` passed.

## Validation boundary

I did not open the desktop GUI, connect to a live Colab worker, or make a new
EXE.  `cmake --preset windows-msvc-release` can now see the MSVC Build Tools
when their developer environment is loaded, but stops at the missing Qt 6.9
development package (`Qt6Config.cmake`).  Therefore the changed C++ test has
not been rebuilt, CTest has not run against this commit, and no package claim
is made.  The existing `0.0.6.3` package remains unchanged.
