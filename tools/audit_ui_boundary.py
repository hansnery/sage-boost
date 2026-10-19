"""Read-only UI render-boundary audit. No game execution or runtime patching."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

import capstone
from capstone import x86 as x86
from port_sites import _find_image, _image_range, mask_code
from verify_sites import Image

# Research anchors in the read-only 1.06 reference, never install addresses.
ROOTS = {"ui_draw": 0x8F0CA, "apt_draw": 0x62928, "apt_core": 0x22244D,
         "apt_pre": 0xA8F98, "apt_post": 0xA9071}
MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def instructions(image, rva, size):
    result = list(MD.disasm(bytes(image.mem[rva:rva + size]), image.base + rva))
    assert sum(i.size for i in result) == size
    return result


def edges(image, body):
    imports = {i.address: (entry.dll.decode(), (i.name or b'ordinal').decode())
               for entry in image.pe.DIRECTORY_ENTRY_IMPORT for i in entry.imports}
    direct, indirect = [], []
    for index, ins in enumerate(body):
        if ins.mnemonic not in ('call', 'jmp') or not ins.operands:
            continue
        operand = ins.operands[0]
        if operand.type == x86.X86_OP_IMM:
            target = operand.imm - image.base
            # Internal branches are implementation details, not outgoing calls.
            if not body[0].address <= operand.imm < body[-1].address + body[-1].size:
                direct.append(target)
        elif operand.type == x86.X86_OP_MEM:
            if not operand.mem.base and not operand.mem.index and operand.mem.disp in imports:
                dll, name = imports[operand.mem.disp]
                indirect.append({"kind": "import", "dll": dll, "name": name})
            else:
                indirect.append({"kind": "indirect", "displacement": operand.mem.disp})
        else:
            resolved = None
            # This compiler keeps timeGetTime's IAT value in a nonvolatile
            # register. Resolve only an unmodified dominating import load;
            # do not guess volatile values across calls or incoming branches.
            if operand.type == x86.X86_OP_REG and operand.reg in (
                    x86.X86_REG_EDI, x86.X86_REG_ESI, x86.X86_REG_EBX, x86.X86_REG_EBP):
                for definition in reversed(body[:index]):
                    if operand.reg not in definition.regs_access()[1]:
                        continue
                    ops = definition.operands
                    if definition.mnemonic == 'mov' and len(ops) == 2 and ops[1].type == x86.X86_OP_MEM:
                        mem = ops[1].mem
                        incoming = [b for b in body if b.group(capstone.CS_GRP_JUMP) and b.operands
                                    and b.operands[0].type == x86.X86_OP_IMM
                                    and definition.address < b.operands[0].imm <= ins.address
                                    and not definition.address <= b.address < ins.address]
                        if not mem.base and not mem.index and mem.disp in imports and not incoming:
                            dll, name = imports[mem.disp]
                            resolved = {"kind": "import", "dll": dll, "name": name}
                    break
            indirect.append(resolved or {"kind": "register"})
    return direct, indirect


def audit(reference, image, sizes):
    lo, hi = _image_range(reference)
    mapped, records, controls = {}, {}, []
    for name, source_rva in ROOTS.items():
        size = sizes[source_rva]
        source = bytes(reference.mem[source_rva:source_rva + size])
        instructions(reference, source_rva, size)
        pattern = mask_code(source, reference.base + source_rva, lo, hi)
        hits = _find_image(image, pattern)
        assert len(hits) == 1, (name, "whole-body match count", len(hits))
        mapped[name] = hits[0]
        body = instructions(image, hits[0], size)
        direct, indirect = edges(image, body)
        records[name] = {"rva": hex(hits[0]), "size": size,
                         "body_sha256": hashlib.sha256(bytes(image.mem[hits[0]:hits[0] + size])).hexdigest(),
                         "whole_body_unique": True,
                         "direct_targets": [hex(v) for v in direct], "indirect_targets": indirect}
        # Reject an altered terminal opcode. Only the owned in-memory copy changes.
        fixed = max(i for i, b in enumerate(pattern) if b is not None)
        at = hits[0] + fixed
        old = image.mem[at]
        image.mem[at] ^= 0x40
        try:
            assert not _find_image(image, pattern), (name, "altered body accepted")
        finally:
            image.mem[at] = old
        controls.append(name + ": altered body refused")
    assert records['apt_draw']['direct_targets'] == [hex(mapped[k]) for k in ('apt_pre', 'apt_core', 'apt_post')]
    pre_imports = [r.get('name') for r in records['apt_pre']['indirect_targets']]
    assert pre_imports.count('timeGetTime') == 2
    assert {"kind": "indirect", "displacement": 0x28} in records['apt_core']['indirect_targets']
    return {"image": str(image.path), "image_sha256": sha(image.path), "functions": records,
            "wrapper_calls_bound_to_verified_bodies": True, "clock_import_calls": 2,
            "conditional_update_slot": "0x28", "alteration_controls": controls}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--reference', type=Path, default=Path('C:/repositories/Open-BFME-2'))
    parser.add_argument('--image', type=Path, action='append', required=True)
    parser.add_argument('--capture', type=Path, required=True, help='Previously copied timing CSV')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert args.output.suffix == '.json'
    assert not args.output.resolve().is_relative_to(args.reference.resolve()), 'Reference checkout is read-only'
    assert args.output.resolve() not in {p.resolve() for p in [args.capture, *args.image]}
    baseline = args.reference / 'baselines/bfme2/workshop-vanilla-1.06/files/game.dat'
    source = Image(baseline)
    sizes_file = args.reference / 'reverse/ghidra_functions.csv'
    with sizes_file.open() as stream:
        sizes = {int(r['rva'], 16): int(r['size']) for r in csv.DictReader(stream)}
    results = [audit(source, Image(path), sizes) for path in args.image]
    with args.capture.open() as stream:
        window = [r for r in csv.DictReader(stream) if 1193 <= float(r['time_s']) <= 1252]
    assert [float(r['time_s']) for r in window] == list(range(1193, 1253))
    samples = sum(int(r['extra_ui_samples']) for r in window)
    extras = sum(int(r['extra_frames']) for r in window)
    assert samples and samples == extras
    sources = [
        'Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplayRva0004A462.cpp',
        'Code/GameEngine/Source/GameClient/InGameUISetEngineInputEnabled.cpp',
        'Code/GameEngine/Source/GameClient/InGameUI.cpp',
        'Code/GameEngine/Source/GameClient/System/Rva0022244D.cpp',
        'Code/GameEngine/Source/GameClient/System/Rva00062928RenderScope.cpp',
        'Code/GameEngineDevice/Source/W3DDevice/GameClient/GUI/W3DAptAuxPreDrawRva000A8F98.cpp',
        'Code/GameEngineDevice/Source/W3DDevice/GameClient/GUI/W3DAptAux_PostDraw.cpp',
    ]
    result = {"kind": "read-only native UI boundary audit", "game_executed": False,
              "runtime_qualified": False, "performance_improvement_measured": False,
              "audit_source_sha256": {p.name: sha(p) for p in [Path(__file__),
                                        Path(__file__).with_name('port_sites.py'),
                                        Path(__file__).with_name('verify_sites.py')]},
              "reference_commit": subprocess.check_output(
                  ['git', '-C', str(args.reference), 'rev-parse', 'HEAD'], text=True).strip(),
              "reference_image_sha256": sha(baseline), "boundary_catalog_sha256": sha(sizes_file),
              "reference_source_sha256": {p: sha(args.reference / p) for p in sources},
              "images": results,
              "preserved_capture": {"path": str(args.capture), "sha256": sha(args.capture),
                                    "window": [1193, 1252], "extra_draws": extras,
                                    "mean_extra_ui_ms": sum(float(r['extra_ui_total_ms']) for r in window) / samples,
                                    "mean_complete_extra_ms": sum(float(r['extra_ms']) * int(r['extra_frames']) for r in window) / extras,
                                    "mean_profiled_extra_ms": sum(float(r['extra_profile_total_ms']) for r in window) /
                                                              sum(int(r['extra_profile_valid']) for r in window)},
              "decision": "Do not cache or skip the complete RenderUI/apt_draw boundary.",
              "reasons": ["APT PreDraw contains two conditional timeGetTime call sites and changes its animation clock.",
                          "APT core conditionally invokes virtual update slot 0x28 and clears its dirty flag.",
                          "UI draw includes preDraw, per-view drawing and window repainting.",
                          "The reference includes custom-control callbacks and mutable mask/transform state."],
              "next_boundary": "Measure rendering beneath update/time/custom-control boundaries; do not assume the full UI scope is reusable.",
              "limits": ["This is static call-boundary evidence, not a live UI profile or a complete call-graph proof.",
                         "Whole-body masks relocate addresses; only the APT wrapper's three child bindings are checked here.",
                         "The copied UI scope includes more than APT; its 1.4 ms cannot be assigned wholly to one child.",
                         "No GPU pixel/state equivalence or runtime optimization is qualified."]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(f'Verified {len(results)} images, five UI boundaries each; whole-UI reuse rejected, no runtime changes.')


if __name__ == '__main__':
    main()
