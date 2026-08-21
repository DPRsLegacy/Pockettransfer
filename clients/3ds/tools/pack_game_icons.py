#!/usr/bin/env python3
"""Download 3DS HOME Menu game icons from Bulbagarden and pack romfs/game_icons.t3x."""
from __future__ import annotations

import json
import struct
import subprocess
import sys
import urllib.parse
import urllib.request
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = Path(__file__).resolve().parent / "sprite-cache" / "games"
ROMFS = ROOT / "romfs"
OUT = ROMFS / "game_icons.t3x"
UA = "PockettransferSpritePacker/1.0"
API = "https://archives.bulbagarden.net/w/api.php"

# Order must match icon indices in clients/shared/games.h.
FILES = [
    "X icon.png",
    "Y icon.png",
    "Omega Ruby icon.png",
    "Alpha Sapphire icon.png",
    "Sun icon.png",
    "Moon icon.png",
    "Ultra Sun icon.png",
    "Ultra Moon icon.png",
    "Red VC icon.png",
    "Blue VC icon.png",
    "Yellow VC icon.png",
    "Green VC icon.png",
    "Gold VC icon.png",
    "Silver VC icon.png",
    "Crystal VC icon.png",
]

# DS titles have no 3DS HOME icons; generate colored 48x48 tiles.
DS_PLACEHOLDERS = [
    ((48, 104, 184), "D"),
    ((216, 120, 168), "P"),
    ((152, 152, 168), "Pt"),
    ((216, 168, 48), "HG"),
    ((176, 176, 184), "SS"),
    ((40, 40, 48), "B"),
    ((232, 232, 232), "W"),
    ((32, 32, 40), "B2"),
    ((244, 244, 248), "W2"),
]


def which_tex3ds() -> str:
    from shutil import which

    found = which("tex3ds")
    if found:
        return found
    p = Path("/opt/devkitpro/tools/bin/tex3ds")
    if p.exists():
        return str(p)
    sys.exit("tex3ds not found; install 3ds-dev (devkitPro)")


def file_url(name: str) -> str | None:
    q = urllib.parse.urlencode(
        {
            "action": "query",
            "titles": f"File:{name}",
            "prop": "imageinfo",
            "iiprop": "url",
            "format": "json",
        }
    )
    req = urllib.request.Request(API + "?" + q, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = json.loads(resp.read().decode())
    for page in data["query"]["pages"].values():
        info = page.get("imageinfo")
        if info:
            return info[0]["url"]
    return None


def fetch(name: str, dest: Path) -> bool:
    if dest.exists() and dest.stat().st_size > 64:
        return True
    try:
        url = file_url(name)
        if not url:
            return False
        req = urllib.request.Request(url, headers={"User-Agent": UA})
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = resp.read()
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            return False
        dest.write_bytes(data)
        print("  fetched", dest.name, len(data), "bytes")
        return True
    except Exception as exc:
        print("  skip", name, exc)
        return False


def png_chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_png_rgb(path: Path, rgb: bytes, w: int, h: int) -> None:
    raw = b""
    stride = w * 3
    for y in range(h):
        raw += b"\x00" + rgb[y * stride : (y + 1) * stride]
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(raw, 9))
        + png_chunk(b"IEND", b"")
    )


FONT = {
    "D": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "t": ["00100", "00100", "11111", "00100", "00100", "00100", "00011"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01110"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "W": ["10001", "10001", "10001", "10001", "10101", "10101", "01010"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
}


def placeholder(path: Path, color: tuple[int, int, int], label: str) -> None:
    w = h = 48
    r, g, b = color
    ink = (248, 248, 248) if (r + g + b) < 360 else (32, 32, 40)
    px = bytearray([r, g, b] * (w * h))
    glyphs = [FONT[ch] for ch in label if ch in FONT]
    total = len(glyphs) * 6 - 1
    ox = max(0, (w - total) // 2)
    oy = (h - 7) // 2
    for gi, glyph in enumerate(glyphs):
        for y, row in enumerate(glyph):
            for x, bit in enumerate(row):
                if bit != "1":
                    continue
                i = ((oy + y) * w + ox + gi * 6 + x) * 3
                px[i : i + 3] = bytes(ink)
    write_png_rgb(path, bytes(px), w, h)
    print("  placeholder", path.name, label)


def main() -> int:
    CACHE.mkdir(parents=True, exist_ok=True)
    ROMFS.mkdir(parents=True, exist_ok=True)
    paths = []
    print("downloading 3DS/VC game icons from archives.bulbagarden.net")
    for i, name in enumerate(FILES):
        dest = CACHE / f"{i}.png"
        if not fetch(name, dest):
            if i == 11:
                fetch("Green VC JP icon.png", dest)
            if not dest.exists() or dest.stat().st_size <= 64:
                placeholder(dest, (80, 120, 72), name.split()[0][:2])
        paths.append(str(dest.resolve()))
    for j, (color, label) in enumerate(DS_PLACEHOLDERS):
        dest = CACHE / f"{len(FILES) + j}.png"
        if not dest.exists() or dest.stat().st_size <= 64:
            placeholder(dest, color, label)
        paths.append(str(dest.resolve()))
    script_mtime = Path(__file__).stat().st_mtime
    if OUT.exists() and OUT.stat().st_size > 64:
        newest = max([Path(p).stat().st_mtime for p in paths] + [script_mtime])
        if OUT.stat().st_mtime >= newest:
            print("using existing", OUT)
            return 0
    tex3ds = which_tex3ds()
    print("packing", OUT, "n=", len(paths))
    subprocess.check_call([tex3ds, "--atlas", "-f", "rgba5551", "-o", str(OUT)] + paths)
    print("wrote", OUT, "bytes", OUT.stat().st_size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
