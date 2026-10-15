#!/usr/bin/env python
"""Research helper (not shipped): retype the pads of Realms' `fortress_carndum.bse`.

    python tools/bse_patch_carndum.py <target.bse> <backup.bse> [--decoded OUT]

Turns every `GuldurFortressExpansionPadSide` into `CarnDumFortressExpansionPadSide`
(on an axis through the centre) or `CarnDumFortressExpansionPadCorner` (on a
diagonal), renames their uniqueIDs to match, and moves `CarnDumFortressCitadel`
to (1000, 1000), angle 0, in both `ObjectsList` and `CastleTemplates`. Every
other chunk is copied byte for byte; enclosing chunk sizes are recomputed and
the result is recompressed (`refpack_encode.ear_encode`).

Works from the backup and refuses to write unless the target still equals it,
so make the backup first. Run once on 2026-10-14; kept as a worked example of
editing a castle file. Check the result with `bse_verify.py` and
`bse_fielddiff.py`.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from mapinfo import Reader, decode
from refpack_encode import ear_encode

CENTER = (1000.0, 1000.0)
OLD_PAD = "GuldurFortressExpansionPadSide"
SIDE = "CarnDumFortressExpansionPadSide"
CORNER = "CarnDumFortressExpansionPadCorner"
CITADEL = "CarnDumFortressCitadel"


def ascii_bytes(s: str) -> bytes:
    raw = s.encode("latin1")
    return struct.pack("<H", len(raw)) + raw


def pad_type(dx: float, dy: float) -> str:
    if abs(dx) < 1e-3 or abs(dy) < 1e-3:
        return SIDE
    if abs(abs(dx) - abs(dy)) < 1e-3:
        return CORNER
    raise ValueError(f"pad offset ({dx}, {dy}) is neither side nor corner")


def dict_entries(r: Reader, pos: int):
    """(key, type, value_start, value_end) for each entry, plus the dict's end."""
    count = struct.unpack_from("<H", r.data, pos)[0]
    pos += 2
    out = []
    for _ in range(count):
        kt = struct.unpack_from("<I", r.data, pos)[0]
        pos += 4
        typ, start = kt & 0xFF, pos
        if typ == 0:
            pos += 1
        elif typ in (1, 2):
            pos += 4
        elif typ == 3:
            pos += 2 + struct.unpack_from("<H", r.data, pos)[0]
        elif typ == 4:
            pos += 2 + 2 * struct.unpack_from("<H", r.data, pos)[0]
        else:
            raise ValueError(f"dict value type {typ}")
        out.append((r.names[kt >> 8], typ, start, pos))
    return out, pos


def chunk(cid_ver_from: bytes, body: bytes) -> bytes:
    cid, ver = struct.unpack_from("<IH", cid_ver_from)
    return struct.pack("<IHI", cid, ver, len(body)) + body


def patch_objects(r: Reader, b0: int, b1: int) -> bytes:
    d = r.data
    out = bytearray()
    for _, _, o0, o1 in r.chunks(b0, b1):
        x, y, z, angle, flags = struct.unpack_from("<ffffI", d, o0)
        typ, after_type = r.ascii(o0 + 20)
        entries, dict_end = dict_entries(r, after_type)
        new_type = typ
        if typ == OLD_PAD:
            new_type = pad_type(x - CENTER[0], y - CENTER[1])
        elif typ == CITADEL:
            x, y, angle = CENTER[0], CENTER[1], 0.0
        body = bytearray(struct.pack("<ffffI", x, y, z, angle, flags))
        body += ascii_bytes(new_type)
        body += d[after_type:after_type + 2]
        for key, vt, vs, ve in entries:
            ks = vs - 4
            if key == "uniqueID" and new_type != typ:
                old_id = d[vs + 2:ve].decode("latin1")
                prefix, _, num = old_id.rpartition(" ")
                assert prefix == typ, old_id
                body += d[ks:vs] + ascii_bytes(f"{new_type} {num}")
            else:
                body += d[ks:ve]
        body += d[dict_end:o1]
        out += chunk(d[o0 - 10:o0 - 4], bytes(body))
    return bytes(out)


def patch_castle_templates(r: Reader, b0: int, b1: int) -> bytes:
    """Entries: name, template (u16 strings), x, y, z, angle (floats), priority, phase (u32)."""
    d = r.data
    key, count = struct.unpack_from("<II", d, b0)
    pos = b0 + 8
    body = bytearray(d[b0:b0 + 8])
    for _ in range(count):
        name, pos = r.ascii(pos)
        tmpl, pos = r.ascii(pos)
        x, y, z, angle = struct.unpack_from("<ffff", d, pos)
        tail = d[pos + 16:pos + 24]
        pos += 24
        if tmpl == OLD_PAD:
            tmpl = pad_type(x, y)
        elif tmpl == CITADEL:
            x, y, angle = 0.0, 0.0, 0.0
        body += ascii_bytes(name) + ascii_bytes(tmpl) + struct.pack("<ffff", x, y, z, angle) + tail
    body += d[pos:b1]
    return bytes(body)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", type=Path)
    ap.add_argument("backup", type=Path)
    ap.add_argument("--decoded", type=Path, help="also write the new decoded stream here")
    args = ap.parse_args()
    orig_file = args.backup.read_bytes()
    if args.target.read_bytes() != orig_file:
        raise SystemExit("target differs from backup; refusing to overwrite")
    d = decode(orig_file)
    r = Reader(d)
    pieces, last = [], 0
    for name, _, b0, b1 in r.chunks(r.body_start, len(d)):
        if name == "ObjectsList":
            new_body = patch_objects(r, b0, b1)
        elif name == "CastleTemplates":
            new_body = patch_castle_templates(r, b0, b1)
        else:
            continue
        pieces += [d[last:b0 - 10], chunk(d[b0 - 10:b0 - 4], new_body)]
        last = b1
    pieces.append(d[last:])
    new = b"".join(pieces)
    out = ear_encode(new)
    if args.decoded:
        args.decoded.write_bytes(new)
    args.target.write_bytes(out)
    print(f"decoded {len(d)} -> {len(new)} bytes (+{len(new) - len(d)}); file {len(orig_file)} -> {len(out)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
