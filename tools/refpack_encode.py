#!/usr/bin/env python
"""Research helper (not shipped): a RefPack compressor for SAGE `.map`/`.bse` files.

    from refpack_encode import ear_encode
    data = ear_encode(decoded)        # `EAR\\0` + size + RefPack stream

The inverse of `mapinfo.refpack`/`mapinfo.decode`. Greedy matching over
3-byte hash chains; the output is valid RefPack (short, medium and long
copies, literal runs, stop code), not byte-identical to EA's compressor.
`ear_encode` checks the round trip through `mapinfo.decode` before
returning. Streams of 16 MiB or more (the 4-byte size variant) are refused.
"""

from __future__ import annotations

import struct

from mapinfo import decode, refpack


def _common_len(data: bytes, p: int, i: int, maxlen: int) -> int:
    lo, hi = 0, maxlen
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if data[p:p + mid] == data[i:i + mid]:
            lo = mid
        else:
            hi = mid - 1
    return lo


def refpack_encode(data: bytes) -> bytes:
    n = len(data)
    if n >= 1 << 24:
        raise ValueError("needs the 4-byte size variant")
    out = bytearray([0x10, 0xFB]) + n.to_bytes(3, "big")
    heads: dict[bytes, list[int]] = {}

    def insert(pos: int) -> None:
        if pos + 3 <= n:
            lst = heads.setdefault(data[pos:pos + 3], [])
            lst.append(pos)
            if len(lst) > 64:
                del lst[:32]

    def flush_literals(start: int, end: int) -> int:
        while end - start > 3:
            k = min(112, (end - start) & ~3)
            out.append(0xE0 | ((k >> 2) - 1))
            out.extend(data[start:start + k])
            start += k
        return start

    lit_start = i = 0
    while i < n:
        best_len = best_off = 0
        cands = heads.get(data[i:i + 3]) if i + 3 <= n else None
        if cands:
            maxlen = min(1028, n - i)
            for p in reversed(cands[-24:]):
                off = i - p
                if off > 131072:
                    break
                ln = _common_len(data, p, i, maxlen)
                ok = (ln >= 3 and off <= 1024) or (ln >= 4 and off <= 16384) or ln >= 5
                if ok and ln > best_len:
                    best_len, best_off = ln, off
                    if ln == maxlen:
                        break
        if not best_len:
            insert(i)
            i += 1
            continue
        lit_start = flush_literals(lit_start, i)
        lit = i - lit_start
        ln, off = best_len, best_off - 1
        if ln <= 10 and off < 1024:
            out += bytes([((off >> 8) << 5) | ((ln - 3) << 2) | lit, off & 0xFF])
        elif ln <= 67 and off < 16384:
            out += bytes([0x80 | (ln - 4), (lit << 6) | (off >> 8), off & 0xFF])
        else:
            out += bytes([0xC0 | ((off >> 16) << 4) | (((ln - 5) >> 8) << 2) | lit,
                          (off >> 8) & 0xFF, off & 0xFF, (ln - 5) & 0xFF])
        out += data[lit_start:i]
        for k in range(i, i + ln):
            insert(k)
        i += ln
        lit_start = i
    lit_start = flush_literals(lit_start, n)
    out.append(0xFC | (n - lit_start))
    out += data[lit_start:n]
    return bytes(out)


def ear_encode(decoded: bytes) -> bytes:
    """A raw `CkMp` stream -> the file bytes the game and WorldBuilder read."""
    stream = refpack_encode(decoded)
    if refpack(stream) != decoded:
        raise ValueError("RefPack round-trip mismatch")
    out = b"EAR\0" + struct.pack("<I", len(decoded)) + stream
    if decode(out) != decoded:
        raise ValueError("EAR round-trip mismatch")
    return out
