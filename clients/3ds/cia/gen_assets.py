#!/usr/bin/env python3
"""Write a short silent WAV into BUILD for the HOME Menu banner."""
from __future__ import annotations

import sys
import wave
from pathlib import Path


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
    write_wav(out / "banner.wav")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
