# AI agent response - Dubbing preview workspace source batch

Date: 2026-08-10

## Result

The Dubbing workspace keeps every existing LA Studio feature and route, but
now gives the video canvas priority instead of letting source/download controls
compress it.

- The default central Preview workspace is wider (`860 px`, minimum `620 px`).
- After a source is loaded, source/download/Chromium controls use a bounded,
  scrollable panel (`160 px` maximum) rather than consuming the video height.
  They remain reachable through **Change / download source**.
- The player has a larger 16:9-oriented minimum/preferred canvas, so subtitle
  OCR's draggable region remains usable.
- **Focus video** temporarily hides History, the stage workspace, and the node
  inspector. **Exit video focus** restores them; no project, route, model, or
  processing state is changed.
- A real horizontal handle between Preview and Timeline lets the user resize
  the Timeline from `96` to `360 px`; dragging it down gives the canvas more
  height.

Source/test commit on `main`: `b86eb90 feat(dubbing): prioritize resizable
video canvas`.

## Reference research

The matching OpenCut reference is available at
`C:/Users/Nguyen Trong Khoi/Downloads/OpenCut-reference`, remote
`https://github.com/OpenCut-app/OpenCut.git`, commit `4d8c49e`.

Its shell follows the CapCut-like composition requested here: Browser/media at
left, Preview in the central upper workspace, Inspector at right, and Timeline
as a separate lower panel. Its web UI also uses explicit resizable-panel
separators. LA Studio now follows those layout principles while retaining its
own Dubbing, Direct Colab, API Gateway, OCR, queue, speaker, and export
features.

## Verification

- QML lint: passed.
- Targeted media/remote/offscreen-QML regression: **4/4 passed**.
- Full CTest: **39/39 passed**.
- The initial QML smoke run exposed an invalid `Button.toolTip` property on
  the new focus control. It was corrected to the attached `ToolTip` API, then
  the QML route loaded and the complete test suite passed.
- `graphify update .` completed after source changes.

This is automated/offscreen evidence. No user GUI, live video, browser,
Douyin, API, or Colab worker was opened for this batch.

## Package status

No EXE was created for this source batch. The retention rule permits at most
three package folders and all three slots are currently occupied:
`LA-Studio-0.0.2.39`, `LA-Studio-0.0.2.40`, and `LA-Studio-0.0.6.0`.

The next package must be `0.0.6.1`. It can be created without overwriting a
candidate once one old package folder is removed by the user; that avoids
violating the explicit three-version limit.
