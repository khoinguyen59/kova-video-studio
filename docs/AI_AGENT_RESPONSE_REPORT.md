# AI agent response - 0.0.2.36 explicit Douyin cookie retry

Date: 2026-08-09

## Result

The current yt-dlp package (`2026.07.04`) still requires fresh cookies for
the tested Douyin short link. The fix keeps the normal no-cookie behavior and
adds an explicit, user-controlled recovery path:

1. In Import/Download or the Dubbing source panel, choose a fresh Netscape
   tab-separated cookie file with **Choose Douyin cookies** before starting a
   download.
2. Paste the raw link or full Douyin share text and add it to the queue.
3. If yt-dlp reports the fresh-cookie diagnostic, the queue item is shown as
   `needs-auth` and exposes **Retry with cookies**. Select the cookie file,
   then press that button; the original link is retained only in memory for
   this retry.

The app never reads Chrome/browser cookie stores. The selected file is checked
for readability, size (1 byte–16 MiB) and Netscape tab-separated structure,
then copied to an owner-only temporary file for the resolver. The temporary
copy is deleted after resolver success/failure/cancel/destruction. No cookie
contents, path, URL, token or credential is written to project/settings/history,
output metadata or logs. On successful retry, the source URL and cookie
configuration are cleared.

## Verification

- Targeted media regression: PASS. It covers default `--no-cookies`, explicit
  `--cookies`, temp-copy cleanup, actionable fresh-cookie diagnostics, and a
  real controller retry from `needs-auth` to a downloaded loopback media file.
- QML lint: PASS.
- Full CTest: **39/39 PASS** after the final source/test changes.
- Portable staging: FileVersion/ProductVersion `0.0.2.36`; yt-dlp
  `2026.07.04`; FFmpeg `N-125829-gfe953596e9-20260728`; qwindows/qoffscreen,
  RuntimeHost, Colab worker templates and 19/19 staging/license artifacts
  verified.

## Artifact

- [LA-Studio-0.0.2.36.exe](C:/Users/Nguyen%20Trong%20Khoi/Downloads/LA-STUDIO/out/LA-Studio-0.0.2.36/LA-Studio-0.0.2.36.exe)
- SHA-256: `745D6776350C2408824126E006FE4449ACACAA4B439C4DD1EE76FC1637B3D7C1`
- Package: internal portable build; eSpeak MSI is hash-verified but unsigned.

## Manual acceptance still needed

I did not open the visible app, browser or a live Douyin/Colab service. The
remaining acceptance is to export a fresh Netscape cookie file from the user's
browser, choose it in `0.0.2.36`, retry the same Douyin link, and confirm the
download becomes selectable. Public-link availability and account/cookie
validity are external conditions and are not claimed by the loopback tests.

## Follow-up implementation - dedicated Douyin Chromium session (2026-08-10)

The next fix follows the browser-session approach researched after the
`yt-dlp` fresh-cookie failures. LA Studio now has an app-owned Playwright
helper at `scripts/douyin_browser_session.py` and a C++ process boundary in
`src/dubbing/media/DouyinBrowserSessionService.*`.

- **Profile isolation:** the session is stored under the LA Studio data
  directory (`~/.lastudio/douyin-browser-profile`, or the explicit
  `LASTUDIO_DOUYIN_BROWSER_PROFILE` override). The code never reads or
  imports Chrome, Edge, or Firefox profiles and never passes
  `--cookies-from-browser`.
- **Explicit lifecycle:** Import/Download and the Dubbing source panel now
  expose **Set up browser session**, **Check connection**, and **Disable**.
  Setup opens the separate managed Chromium profile for the user to sign in;
  Check verifies an authenticated Douyin session; only a verified session is
  allowed to download Douyin pages through the browser worker.
- **Dynamic page path:** the helper opens the Douyin page with Playwright,
  captures a real video response/`video` resource, and streams it to LA Studio
  staging with the managed session's cookies and referer. Signed URLs and
  cookie values are not emitted to the app or logs. The source URL remains a
  short-lived process argument and is not persisted.
- **Fallback remains explicit:** normal links still use the managed yt-dlp
  adapter with `--no-cookies`; the existing user-selected Netscape cookie file
  remains available as a separate recovery path. There is no silent Local or
  browser fallback when a route was not verified.
- **Packaging:** `scripts/package.ps1` stages the helper under
  `douyin-browser/douyin_browser_session.py`. Python Playwright and its
  Chromium binary are intentionally user-installed dependencies; the app does
  not download or control a user's existing browser.

### Verification for this follow-up

- `python -m py_compile scripts/douyin_browser_session.py`: PASS.
- Helper contract checks: dedicated `--profile`/`--mode`/`--url`/`--output`
  arguments only; no browser-cookie import flags: PASS.
- Targeted `TestMediaIngestService` + `PrepareQmlRouteSmokeRuntime` +
  `QmlRouteSmoke`: **3/3 PASS**.
- Full CTest: **39/39 PASS** after rebuilding the current source.
- QML lint: PASS.
- `graphify update .`: completed; graph updated to 12,648 nodes / 25,403
  edges.

No visible GUI, user browser profile, login, or live authenticated Douyin
download was performed. Manual acceptance is still required: install
Playwright/Chromium in the configured Python environment, click **Set up
browser session**, sign in in the separate window, close it, click **Check
connection**, then download one Douyin share link. The current source is
verified; no new versioned EXE was packaged in this follow-up.
