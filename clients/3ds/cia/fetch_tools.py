#!/usr/bin/env python3
"""Download Linux x86_64 makerom + bannertool into cia/tools/ if missing."""
from __future__ import annotations

import io
import stat
import urllib.request
import zipfile
from pathlib import Path

MAKEROM_URL = (
    "https://github.com/3DSGuy/Project_CTR/releases/download/"
    "makerom-v0.19.0/makerom-v0.19.0-ubuntu_x86_64.zip"
)
BANNERTOOL_URL = (
    "https://github.com/Epicpkmn11/bannertool/releases/download/"
    "v1.2.2/bannertool.zip"
)

TOOLS = Path(__file__).resolve().parent / "tools"


def _download(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "pockettransfer-cia"})
    with urllib.request.urlopen(req) as resp:
        return resp.read()


def _chmod_x(path: Path) -> None:
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def _extract_named(blob: bytes, dest: Path, *name_tail: str) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(blob)) as zf:
        matches = []
        for info in zf.infolist():
            name = info.filename.replace("\\", "/")
            base = name.rsplit("/", 1)[-1]
            if info.is_dir() or base not in name_tail:
                continue
            lower = name.lower()
            if any(s in lower for s in ("windows", "macos", "osx", "mac-")):
                continue
            matches.append(info)
        if not matches:
            raise SystemExit(f"could not find {name_tail} in zip")
        def rank(info: zipfile.ZipInfo) -> int:
            n = info.filename.lower()
            if "x86_64" in n or "amd64" in n:
                return 0
            if "aarch64" in n or "arm64" in n:
                return 2
            if "i686" in n or "i386" in n:
                return 3
            return 1
        info = sorted(matches, key=rank)[0]
        dest.write_bytes(zf.read(info))
        _chmod_x(dest)


def main() -> int:
    TOOLS.mkdir(parents=True, exist_ok=True)
    makerom = TOOLS / "makerom"
    bannertool = TOOLS / "bannertool"
    if not makerom.is_file():
        print("Downloading makerom…")
        _extract_named(_download(MAKEROM_URL), makerom, "makerom")
    if not bannertool.is_file():
        print("Downloading bannertool…")
        _extract_named(_download(BANNERTOOL_URL), bannertool, "bannertool")
    print(f"CIA tools ready in {TOOLS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
