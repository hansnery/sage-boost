#!/usr/bin/env python
"""Tests for refpack_encode.py on synthetic inputs (no game files needed).

    python tools/test_refpack_encode.py
"""

from __future__ import annotations

import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import mapinfo  # noqa: E402
from refpack_encode import ear_encode, refpack_encode  # noqa: E402


def cases() -> list[bytes]:
    rng = random.Random(1)
    noise = bytes(rng.randrange(256) for _ in range(5000))
    words = b" ".join(rng.choice([b"CkMp", b"ObjectsList", b"uniqueID", b"\0\0\0\0"]) for _ in range(4000))
    return [b"", b"a", b"abc", b"abcd", b"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", noise, words,
            noise[:3000] + noise[:3000] + bytes(70000) + noise[100:2000], b"x" * 200000]


def main() -> int:
    for data in cases():
        stream = refpack_encode(data)
        assert stream[:2] == b"\x10\xfb" and int.from_bytes(stream[2:5], "big") == len(data)
        assert mapinfo.refpack(stream) == data, len(data)
    raw = b"CkMp" + struct.pack("<I", 0) + bytes(1000)
    out = ear_encode(raw)
    assert out[:4] == b"EAR\0" and struct.unpack_from("<I", out, 4)[0] == len(raw)
    assert mapinfo.decode(out) == raw
    assert len(refpack_encode(b"x" * 200000)) < 2000
    try:
        refpack_encode(bytes(1 << 24))
    except ValueError:
        pass
    else:
        raise AssertionError("16 MiB input accepted")
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
