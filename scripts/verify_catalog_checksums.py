#!/usr/bin/env python3
"""Fail when a GitHub release download in the generated catalog lacks SHA-256."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / "data" / "catalog.json"


def walk(value, path=()):
    if isinstance(value, dict):
        url = value.get("url") or value.get("downloadUrl")
        if isinstance(url, str) and "releases/download" in url:
            checksum = value.get("sha256")
            if not isinstance(checksum, str) or len(checksum) != 64 or any(
                character not in "0123456789abcdefABCDEF" for character in checksum
            ):
                yield "/".join(map(str, path)), url
        for key, child in value.items():
            yield from walk(child, path + (key,))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from walk(child, path + (index,))


def main():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    missing = list(walk(catalog))
    if missing:
        for path, url in missing:
            print(f"Missing SHA-256 at {path}: {url}")
        raise SystemExit(f"{len(missing)} release download(s) lack a valid SHA-256.")
    print("Catalog release download SHA-256 verification: passed")


if __name__ == "__main__":
    main()
