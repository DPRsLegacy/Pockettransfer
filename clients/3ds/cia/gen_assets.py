#!/usr/bin/env python3
"""Write a 256x128 HOME Menu banner PNG and a short silent WAV into BUILD."""
from __future__ import annotations

import struct
import sys
import wave
import zlib
from pathlib import Path


def png_chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_png(path: Path, width: int, height: int, pixel) -> None:
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixel(x, y))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + png_chunk(b"IEND", b"")
    )


def banner_pixel(x: int, y: int) -> bytes:
    # Dark bank-box look: navy field, teal top bar, two "screens".
    if y < 10:
        return bytes((0x2E, 0xC4, 0xB2))
    if 28 <= y <= 62 and 24 <= x <= 120:
        return bytes((0x3D, 0x7A, 0xC4))
    if 72 <= y <= 106 and 24 <= x <= 120:
        return bytes((0x3D, 0x7A, 0xC4))
    if 28 <= y <= 106 and 148 <= x <= 232:
        return bytes((0xC4, 0x3D, 0x4A))
    return bytes((0x1A, 0x15, 0x30))


def write_wav(path: Path, seconds: float = 1.5) -> None:
    frames = int(44100 * seconds)
    with wave.open(str(path), "w") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(44100)
        wav.writeframes(b"\x00\x00" * 2 * frames)


def main() -> int:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build")
    out.mkdir(parents=True, exist_ok=True)
    write_png(out / "banner.png", 256, 128, banner_pixel)
    write_wav(out / "banner.wav")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
