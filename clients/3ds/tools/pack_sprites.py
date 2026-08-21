#!/usr/bin/env python3
"""Download Gen 7 PC icons and pack them into romfs/pkm_icons.t3x."""
from __future__ import annotations

import struct
import subprocess
import sys
import urllib.request
import zlib
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = Path(__file__).resolve().parent / "sprite-cache"
ROMFS = ROOT / "romfs"
OUT = ROMFS / "pkm_icons.t3x"
T3S = CACHE / "pkm_icons.t3s"
MAX_SPECIES = 807
ICON_URL = (
    "https://raw.githubusercontent.com/PokeAPI/sprites/master/"
    "sprites/pokemon/versions/generation-vii/icons/{n}.png"
)
UA = "PockettransferSpritePacker/1.0"


def png_chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_rgba_png(path: Path, width: int, height: int, pixel) -> None:
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixel(x, y))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + png_chunk(b"IEND", b"")
    )


def placeholder(path: Path, species: int) -> None:
    h = (species * 2654435761) & 0xFFFFFFFF

    def px(x: int, y: int) -> bytes:
        if x < 1 or y < 1 or x > 38 or y > 28:
            return b"\x00\x00\x00\x00"
        cx, cy = 20, 16
        dx, dy = x - cx, y - cy
        if dx * dx + dy * dy * 2 > 170:
            return b"\x00\x00\x00\x00"
        r = 70 + (h & 127)
        g = 80 + ((h >> 8) & 127)
        b = 70 + ((h >> 16) & 127)
        if dx * dx + (dy + 4) * (dy + 4) < 8:
            return bytes((20, 20, 20, 255))
        return bytes((r, g, b, 255))

    write_rgba_png(path, 40, 30, px)


def fetch_one(n: int) -> tuple[int, bool]:
    dest = CACHE / f"{n}.png"
    if dest.exists() and dest.stat().st_size > 32:
        return n, True
    req = urllib.request.Request(ICON_URL.format(n=n), headers={"User-Agent": UA})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = resp.read()
        if data[:8] == b"\x89PNG\r\n\x1a\n":
            dest.write_bytes(data)
            return n, True
    except Exception:
        pass
    placeholder(dest, n)
    return n, False


def which_tex3ds() -> str:
    from shutil import which

    found = which("tex3ds")
    if found:
        return found
    p = Path("/opt/devkitpro/tools/bin/tex3ds")
    if p.exists():
        return str(p)
    sys.exit("tex3ds not found; install 3ds-dev (devkitPro)")


def main() -> int:
    CACHE.mkdir(parents=True, exist_ok=True)
    ROMFS.mkdir(parents=True, exist_ok=True)

    write_rgba_png(CACHE / "0.png", 40, 30, lambda x, y: b"\x00\x00\x00\x00")

    missing = [n for n in range(1, MAX_SPECIES + 1) if not (CACHE / f"{n}.png").exists()]
    ok = MAX_SPECIES - len(missing)
    if missing:
        print(f"downloading {len(missing)} Gen 7 box icons ({ok} cached)...")
        with ThreadPoolExecutor(max_workers=12) as pool:
            futs = [pool.submit(fetch_one, n) for n in missing]
            got = 0
            for fut in as_completed(futs):
                n, real = fut.result()
                got += 1
                if got % 80 == 0 or got == len(missing):
                    print(f"  {got}/{len(missing)}")
                if not real:
                    print(f"  placeholder for #{n}")
    else:
        print("sprite cache complete")

    names = [str((CACHE / f"{n}.png").resolve()) for n in range(0, MAX_SPECIES + 1)]
    T3S.write_text("\n".join(names) + "\n")

    tex3ds = which_tex3ds()
    print("packing", OUT)
    cmd = [tex3ds, "--atlas", "-f", "rgba5551", "-o", str(OUT)] + names
    subprocess.check_call(cmd)
    print("wrote", OUT, "bytes", OUT.stat().st_size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
