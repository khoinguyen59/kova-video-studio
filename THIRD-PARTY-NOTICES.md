# LA Studio third-party notices

This notice describes third-party components that reach an LA Studio user in a
Windows release. It distinguishes software shipped with the installer from
software downloaded separately at the user's request. Model and runtime
catalog entries have their own upstream license and redistribution terms; this
notice does not replace those terms.

## Bundled with the Windows installer

| Component | Version / provenance | License | Source / notice |
|---|---|---|---|
| Qt runtime | Qt 6.9.3, deployed by `windeployqt` | LGPL-3.0-only or Qt commercial terms | [Qt licensing](https://doc.qt.io/qt-6/licensing.html) |
| libcurl | vcpkg manifest dependency, baseline `9a023fa7…` | curl license (MIT-style) | [curl COPYING](https://github.com/curl/curl/blob/master/COPYING) |
| zlib | vcpkg manifest dependency, baseline `9a023fa7…` | zlib | [zlib license](https://zlib.net/zlib_license.html) |
| bzip2 | vcpkg manifest dependency, baseline `9a023fa7…`; used by bundled bsdtar | bzip2 license | [bzip2 source](https://sourceware.org/bzip2/) |
| 7-Zip | 26.2, installed by release CI and staged for archive handling | LGPL-2.1-or-later; some code has additional unRAR restrictions | [7-Zip license](https://www.7-zip.org/license.txt) |
| libarchive / bsdtar | libarchive 3.8.1, built during packaging from SHA-256-pinned upstream source | BSD-2-Clause | [libarchive source](https://github.com/libarchive/libarchive/releases/tag/v3.8.1) |
| FFmpeg / FFprobe | BtbN win64 LGPL shared build, SHA-256-pinned and extracted during packaging | LGPL-3.0-or-later | [BtbN FFmpeg Builds](https://github.com/BtbN/FFmpeg-Builds) |
| yt-dlp | 2026.07.04 standalone executable, SHA-256-pinned and staged only for local public video-page resolution | Unlicense | [yt-dlp releases](https://github.com/yt-dlp/yt-dlp/releases) |
| VietNorm text normalization | Port of `vietnormalizer` 0.2.3 commit `dd38778731d6ca4e9e670a19abb2df1c901a1852` and `nghitts` material | MIT and Apache-2.0 | `src/textnorm/UPSTREAM.md`, `licenses/vietnorm/NOTICE` |

## External, user-supplied runtime

| Component | Delivery and verification | License | Source / notice |
|---|---|---|---|
| Tesseract OCR and trained language data | Not bundled and never downloaded by LA Studio. The staged `subtitle-ocr/runtime-manifest.json` and `subtitle-ocr/README.txt` document the local deployment paths; the app runs `tesseract --list-langs` before work begins. | Apache-2.0 | [Tesseract OCR](https://github.com/tesseract-ocr/tesseract), [language data](https://github.com/tesseract-ocr/tessdata) |

## Installer-staged runtime

| Component | Version / provenance | License | Source / notice |
|---|---|---|---|
| eSpeak NG | 1.52.0 MSI, SHA-256-pinned in `scripts/runtime_helpers.ps1` | GPL-3.0-or-later | [eSpeak NG](https://github.com/espeak-ng/espeak-ng) |

The release workflow intentionally rejects an eSpeak MSI without a valid
Authenticode signature. A release is not eligible if that integrity gate
fails, even when this notice is present.

## Downloaded at the user's request

Model files and native inference runtimes are downloaded from sources recorded
in `data/catalog.json`. Each executable runtime download must have a SHA-256
in the catalog before LA Studio accepts it. The catalog must provide the
upstream license, gating, and commercial-use information before a production
release; review the selected model or runtime's upstream terms before use or
redistribution.

## Public media download

Public media-page downloads run through LA Studio's bundled, SHA-256-pinned
`yt-dlp` adapter on the local CPU; they do not use Colab, a GPU, or API
Gateway. When a provider needs login cookies, the user must explicitly choose
a Netscape `cookies.txt` export. LA Studio makes a temporary private copy for
that download attempt and never reads browser profiles or browser cookies.
Media made in a user-run Colab notebook must be downloaded from Colab's Files
sidebar using the output path printed by that notebook, then selected as a
local file in LA Studio.

## License texts and source

The installed `licenses/` directory contains the license material available at
package time, including LA Studio's AGPL-3.0-only license and the VietNorm and
libarchive notices. The corresponding source archive and this notice are
attached to each release. See `docs/RELEASE.md` for the release-owner checks.
