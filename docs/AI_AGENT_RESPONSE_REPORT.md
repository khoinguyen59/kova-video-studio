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
