#!/usr/bin/env python
"""Research helper (not shipped): the players, teams and scripts of a SAGE map.

    python tools/mapinfo.py <file.map | archive.big> [--scripts] [--filter TEXT]

A `.big` argument lists every map inside the archive (`--filter` keeps the
ones whose path contains TEXT). Prints one line per map player:
`name  faction  human|ai  scripts=N  teams=N`; with `--scripts` also each
script's name and the strings it uses (its parameters, read heuristically).
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path


# --------------------------------------------------------------------------- RefPack


def refpack(src: bytes) -> bytes:
    flags = src[0]
    size_len = 4 if flags & 0x80 else 3
    pos = 2 + (size_len if flags & 0x01 else 0)
    out_len = int.from_bytes(src[pos:pos + size_len], "big")
    pos += size_len
    out = bytearray()
    while pos < len(src) and len(out) < out_len:
        b0 = src[pos]
        stop = False
        if b0 < 0x80:
            b1 = src[pos + 1]
            pos += 2
            lit, n, off = b0 & 3, ((b0 >> 2) & 7) + 3, ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xC0:
            b1, b2 = src[pos + 1], src[pos + 2]
            pos += 3
            lit, n, off = b1 >> 6, (b0 & 0x3F) + 4, ((b1 & 0x3F) << 8) + b2 + 1
        elif b0 < 0xE0:
            b1, b2, b3 = src[pos + 1], src[pos + 2], src[pos + 3]
            pos += 4
            lit, n, off = b0 & 3, ((b0 & 0x0C) << 6) + b3 + 5, ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xFC:
            pos += 1
            lit, n, off = ((b0 & 0x1F) + 1) << 2, 0, 0
        else:
            pos += 1
            lit, n, off, stop = b0 & 3, 0, 0, True
        out += src[pos:pos + lit]
        pos += lit
        for _ in range(n):
            out.append(out[-off])
        if stop:
            break
    return bytes(out)


def decode(data: bytes) -> bytes:
    """A map file's bytes -> the raw `CkMp` stream."""
    if data[:4] == b"EAR\0":
        return refpack(data[8:])
    return data


def load_map(path: Path) -> bytes:
    return decode(path.read_bytes())


# --------------------------------------------------------------------------- BIG archives


class BigArchive:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if self.data[:4] not in (b"BIGF", b"BIG4"):
            raise ValueError(f"{path}: not a BIG archive")
        count = struct.unpack_from(">I", self.data, 8)[0]
        pos = 16
        self.entries: list[tuple[str, int, int]] = []
        for _ in range(count):
            off, size = struct.unpack_from(">II", self.data, pos)
            end = self.data.index(b"\0", pos + 8)
            self.entries.append((self.data[pos + 8:end].decode("latin1"), off, size))
            pos = end + 1

    def maps(self) -> dict[str, bytes]:
        return {name: self.data[off:off + size] for name, off, size in self.entries
                if name.lower().endswith(".map")}


# --------------------------------------------------------------------------- chunks


@dataclass
class Script:
    name: str
    strings: list[str] = field(default_factory=list)


@dataclass
class Side:
    name: str
    faction: str
    is_human: bool
    dict: dict[str, object]
    scripts: list[Script] = field(default_factory=list)
    teams: list[str] = field(default_factory=list)


@dataclass
class MapInfo:
    sides: list[Side]
    chunks: list[str]


class Reader:
    def __init__(self, data: bytes):
        if data[:4] != b"CkMp":
            raise ValueError("not a CkMp map stream")
        self.data = data
        count = struct.unpack_from("<I", data, 4)[0]
        pos = 8
        self.names: dict[int, str] = {}
        for _ in range(count):
            ln = data[pos]
            name = data[pos + 1:pos + 1 + ln].decode("latin1")
            self.names[struct.unpack_from("<I", data, pos + 1 + ln)[0]] = name
            pos += 1 + ln + 4
        self.body_start = pos

    def chunks(self, start: int, end: int):
        """(name, version, body_start, body_end) for each chunk in [start, end)."""
        pos = start
        while pos + 10 <= end:
            cid, ver, size = struct.unpack_from("<IHI", self.data, pos)
            body = pos + 10
            if body + size > end:
                raise ValueError(f"chunk at {pos:#x} overruns its parent")
            yield self.names.get(cid, f"?{cid}"), ver, body, body + size
            pos = body + size

    def chunk_sequence_ok(self, start: int, end: int, allowed: set[str]) -> bool:
        pos = start
        while pos < end:
            if pos + 10 > end:
                return False
            cid, _, size = struct.unpack_from("<IHI", self.data, pos)
            if self.names.get(cid) not in allowed or pos + 10 + size > end:
                return False
            pos += 10 + size
        return pos == end

    def ascii(self, pos: int) -> tuple[str, int]:
        ln = struct.unpack_from("<H", self.data, pos)[0]
        return self.data[pos + 2:pos + 2 + ln].decode("latin1"), pos + 2 + ln

    def dict(self, pos: int) -> tuple[dict[str, object], int]:
        count = struct.unpack_from("<H", self.data, pos)[0]
        pos += 2
        out: dict[str, object] = {}
        for _ in range(count):
            kt = struct.unpack_from("<I", self.data, pos)[0]
            pos += 4
            key, typ = self.names.get(kt >> 8, f"?{kt >> 8}"), kt & 0xFF
            if typ == 0:
                value: object = bool(self.data[pos])
                pos += 1
            elif typ == 1:
                value = struct.unpack_from("<i", self.data, pos)[0]
                pos += 4
            elif typ == 2:
                value = struct.unpack_from("<f", self.data, pos)[0]
                pos += 4
            elif typ == 3:
                value, pos = self.ascii(pos)
            elif typ == 4:
                ln = struct.unpack_from("<H", self.data, pos)[0]
                value = self.data[pos + 2:pos + 2 + 2 * ln].decode("utf-16-le")
                pos += 2 + 2 * ln
            else:
                raise ValueError(f"dict value type {typ} at {pos:#x}")
            out[key] = value
        return out, pos


def strings_in(data: bytes, start: int, end: int) -> list[str]:
    """Length-prefixed (u16) printable ASCII strings of 3+ characters."""
    found: list[str] = []
    pos = start
    while pos + 2 <= end:
        ln = struct.unpack_from("<H", data, pos)[0]
        if 3 <= ln <= 200 and pos + 2 + ln <= end:
            raw = data[pos + 2:pos + 2 + ln]
            if all(0x20 <= c <= 0x7E for c in raw):
                s = raw.decode("ascii")
                if s not in found:
                    found.append(s)
                pos += 2 + ln
                continue
        pos += 1
    return found


def read_scripts(r: Reader, start: int, end: int) -> list[Script]:
    out: list[Script] = []
    for name, _, b0, b1 in r.chunks(start, end):
        if name == "Script":
            title, after = r.ascii(b0)
            out.append(Script(title, [s for s in strings_in(r.data, after, b1) if s != title]))
        elif name == "ScriptGroup":
            title, after = r.ascii(b0)
            child = next((p for p in range(after, b1 + 1)
                          if r.chunk_sequence_ok(p, b1, {"Script", "ScriptGroup"})), b1)
            for s in read_scripts(r, child, b1):
                s.name = f"{title}/{s.name}"
                out.append(s)
    return out


def parse(data: bytes) -> MapInfo:
    r = Reader(data)
    top = list(r.chunks(r.body_start, len(data)))
    sides: list[Side] = []
    for name, _, b0, b1 in top:
        if name == "SidesList":
            count = struct.unpack_from("<I", data, b0 + 1)[0]
            pos = b0 + 5
            for _ in range(count):
                d, pos = r.dict(pos)
                builds = struct.unpack_from("<I", data, pos)[0]
                pos += 4
                if builds:
                    raise ValueError("SidesList build-list entries are not decoded")
                sides.append(Side(str(d.get("playerName", "")), str(d.get("playerFaction", "")),
                                  bool(d.get("playerIsHuman", False)), d))
    for name, _, b0, b1 in top:
        if name == "PlayerScriptsList":
            lists = [(c0, c1) for n, _, c0, c1 in r.chunks(b0, b1) if n == "ScriptList"]
            for s, (c0, c1) in zip(sides, lists):
                s.scripts = read_scripts(r, c0, c1)
        elif name == "Teams":
            count = struct.unpack_from("<I", data, b0)[0]
            pos = b0 + 4
            by_name = {s.name: s for s in sides}
            for _ in range(count):
                d, pos = r.dict(pos)
                owner = by_name.get(str(d.get("teamOwner", "")))
                if owner is not None:
                    owner.teams.append(str(d.get("teamName", "")))
    return MapInfo(sides, [n for n, *_ in top])


# --------------------------------------------------------------------------- CLI


def show(title: str, data: bytes, scripts: bool) -> None:
    info = parse(decode(data))
    print(f"== {title}")
    for s in info.sides:
        print(f"  {s.name:28} {s.faction:20} {'human' if s.is_human else 'ai':5} "
              f"scripts={len(s.scripts):<4} teams={len(s.teams)}")
        if scripts:
            for sc in s.scripts:
                print(f"      {sc.name}: {', '.join(sc.strings)}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", type=Path)
    ap.add_argument("--scripts", action="store_true")
    ap.add_argument("--filter", default="")
    args = ap.parse_args()
    if args.path.suffix.lower() == ".big":
        for name, data in BigArchive(args.path).maps().items():
            if args.filter.lower() in name.lower():
                show(name, data, args.scripts)
    else:
        show(str(args.path), args.path.read_bytes(), args.scripts)
    return 0


if __name__ == "__main__":
    sys.exit(main())
