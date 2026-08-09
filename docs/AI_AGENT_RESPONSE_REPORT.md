# AI agent response - 0.0.2.35 shared-link Dubbing library

Date: 2026-08-09

## Result

The copied Douyin sharing text is now accepted directly. For example, pasting
the complete message containing `https://v.douyin.com/uZt0JMXfADs/` causes LA
Studio to extract and queue only that URL. The surrounding short code,
caption and “copy this link” instructions are discarded before the download
resolver sees them.

The Dubbing screen now separates downloading from production work:

1. Paste one or more raw links or complete share messages, then click
   **Add link(s) to download queue**. This step only downloads.
2. Wait for an item to show as downloaded, then click **Downloaded media &
   actions**.
3. Tick exactly the downloaded videos you want, choose one action, then click
   **Run selected action**. A later action can choose a different subset.

The available one-action choices are Import/Normalize, Isolator,
Transcribe/STT, Translate, TTS/Voice and Export/Output. Their prerequisites
are intentional: for example, Translate needs that item's earlier STT output,
and Export needs its earlier generated voice WAV. The previous end-to-end
batch remains under **Full workflow (advanced)** for the cases where it is
wanted.

## Fixed package issue

The earlier `not-ready` items were not selectable because the downloads had
already failed: the app searched for `yt-dlp.exe` in `media-tools`, while the
portable package correctly staged it beside the app executable. The runtime
lookup now follows the real package layout. This is independent of whether a
specific public Douyin URL is currently downloadable.

## Verification

- Controller regression: full share text containing a public link downloads
  exactly the extracted URL into owned staging.
- Controller regression: two imported items can later select only one for an
  action; the other retains its completed project/artifacts and is not rerun.
- Targeted media/Dubbing tests: PASS. QML lint: PASS. Full CTest: **39/39
  PASS** (57.98 s). `graphify update .` completed after edits.
- Package audit: FileVersion and ProductVersion both `0.0.2.35`; root
  `yt-dlp` reports `2026.07.04`; FFmpeg reports successfully; `qwindows`,
  `qoffscreen`, RuntimeHost and the staged manifests are present.

## Artifact and delivery

- [LA-Studio-0.0.2.35.exe](C:/Users/Nguyen%20Trong%20Khoi/Downloads/LA-STUDIO/out/LA-Studio-0.0.2.35/LA-Studio-0.0.2.35.exe)
- SHA-256: `47BED51542DF28B2F5B7EFE0475C221C7DBFF1D96BC7E722D02D9BE9A79B1741`
- Source/test commit: `5e80743 fix: support shared links and independent dubbing actions`
- Branch: `main`, pushed to `origin/main`.

## Manual acceptance still needed

I did not open the visible app or invoke a live Douyin/Colab/API service.
Test the copied message above in `0.0.2.35`, wait for a downloaded state,
select an item, then run the chosen action with your already configured route.
If a public site blocks or changes access, the app will report that concrete
download error rather than marking it selectable.
