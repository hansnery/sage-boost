"""Join the audited native rendering boundaries into runtime metadata.

Reference RVAs are research identities only. Emit signatures, hashes,
region sizes and dependency roles; never persist executable bodies.
"""
import contextlib
import io
import os
import struct
import zlib
from pathlib import Path
import test_group_contract as group
import test_mesh_draw_contract as mesh
import native_fx_state_contract as state
import test_fx_lifecycle as fx
from verify_sites import Image
from test_fx_lifecycle import BASELINE, fnv1a

ROOT = Path(__file__).resolve().parents[1]


def graph(reference, target):
    with contextlib.redirect_stdout(io.StringIO()):
        old_group = group.symbols_for(reference, reference)
        new_group, gr = group.verify(reference, target)
        mr, jump_offsets, new_mesh = mesh.verify(reference, target)
        fr = state.verify(reference, target)
    old_mesh = [r for _, r, *_ in mesh.BODIES + mesh.DEPENDENCIES]
    old_mesh += [r for _, r, _, _ in mesh.DATA] + [0x120584]
    old_fx = [s.reference for s in fx.FUNCTIONS] + list(fx.DEPENDENCIES)
    old_fx += list(state.DEPENDENCIES)
    old_fx += [r for _, r, _ in state.DATA]
    locations = {}
    nodes = {}
    patches = {}

    def locate(old, new):
        assert locations.setdefault(old, new) == new, 'joined identity differs'

    def add(old, size, name, immutable=False, writable=False, digest_size=None):
        code = any(lo <= old < hi for lo, hi in reference.exec_ranges)
        item = nodes.get(old)
        if item:
            assert item['size'] == size and item['immutable'] == immutable, name
            item['writable'] |= writable
        else:
            nodes[old] = dict(size=size, name=name, immutable=immutable,
                              writable=writable, code=int(code), digest_size=digest_size or size)

    for old, new in zip(old_group, new_group):
        locate(old, new)
    for old, new in zip(old_mesh, new_mesh):
        locate(old, new)
    for i, (name, old, size) in enumerate(group.BODIES):
        add(old, size, 'group_' + name, True)
        patches[old] = [(off, 0, old_group[sym]) for off, sym in group.CALLS[i]]
        patches[old] += [(off, 1, old_group[sym]) for off, sym in group.ABSOLUTE[i]]
    for i, (name, old, size) in enumerate(mesh.BODIES):
        add(old, size, 'mesh_' + name, True)
        patches[old] = [(off, kind, old_mesh[sym] + extra) for off, kind, sym, extra in mr[i][3]]
    for i, record in enumerate(fr):
        old = old_fx[i]
        new, size, _, relocations = record
        locate(old, new)
        add(old, size, 'fx_' + (fx.FUNCTIONS[i].name if i < 8 else 'apply_state'), True)
        patches[old] = []
        for off, kind, sym, extra in relocations:
            value = struct.unpack_from('<I' if kind else '<i', target.mem, new + off)[0]
            dest = value - target.base if kind else new + off + 4 + value
            locate(old_fx[sym], dest - extra)
            patches[old].append((off, kind, old_fx[sym] + extra))
    for name, old, size, literal in mesh.DATA:
        add(old, size, name, bool(literal), not literal)
    for name, old, size in state.DATA:
        add(old, size, name, False, True)
    for i, label in enumerate(group.LABELS):
        add(old_group[20 + i], len(label), 'label_' + str(i), True)
    # The event copies four words, of which 13 bytes are the string.
    add(old_group[28], 16, 'event_prefix', True, digest_size=13)
    for i in (26, 32, 33):
        add(old_group[i], 4, 'slot_' + str(i), False, True)
    add(old_mesh[-1], 32, 'draw_targets', True)
    patches[old_mesh[-1]] = [(i * 4, 1, mesh.BODIES[2][1] + off) for i, off in enumerate(jump_offsets)]
    # Preserve complete CRT thunks and independently verify their import names.
    imports = []
    for index, name in ((17, '_mbscpy'), (18, '_mbscat')):
        old, new = old_group[index], new_group[index]
        old_iat = group.operand(reference, old + 2)
        new_iat = group.operand(target, new + 2)
        locate(old_iat, new_iat)
        add(old, 6, 'import_' + name, True)
        patches[old] = [(2, 1, old_iat)]
        imports.append((old_iat, name))
    imports += [(old_group[25], 'sprintf'), (old_group[27], 'strncpy')]
    for old, name in imports:
        add(old, 4, 'iat_' + name)

    def owner(address):
        found = [(old, address - old) for old, node in nodes.items() if old <= address < old + node['size']]
        assert len(found) <= 1, ('overlapping graph', hex(address), found)
        return found[0] if found else None

    for entries in patches.values():
        for _, _, address in entries:
            if not owner(address):
                assert address in locations, ('unlocated external', hex(address))
                add(address, 1, 'external_' + str(len(nodes)))
    # Code/hash regions first, followed by mutable and external identities.
    identities = [r for r, n in nodes.items() if n['immutable']]
    count = len(identities)
    identities += [r for r, n in nodes.items() if not n['immutable']]
    indices = {r: i for i, r in enumerate(identities)}
    entries = []
    for old in identities[:count]:
        node = nodes[old]
        new = locations[old]
        size = node['digest_size']
        before, after = bytearray(reference.mem[old:old + size]), bytearray(target.mem[new:new + size])
        relocations = []
        for off, kind, address in patches.get(old, []):
            base, extra = owner(address)
            assert off + 4 <= size
            value = struct.unpack_from('<I' if kind else '<i', after, off)[0]
            actual = value - target.base if kind else new + off + 4 + value
            assert actual == locations[base] + extra, ('joined dependency differs', node['name'])
            before[off:off + 4] = after[off:off + 4] = b'\0' * 4
            relocations.append((off, kind, indices[base], extra))
        assert before == after, ('joined contents differ', node['name'])
        entries.append((new, size, fnv1a(target.mem[new:new + size]), fnv1a(after), zlib.crc32(after), relocations))
    roots = [group.BODIES[0][1]] + [fx.FUNCTIONS[i].reference for i in (1, 2, 3, 4, 5)]
    signatures = []
    for root in roots:
        index = indices[root]
        length = min(48, nodes[root]['digest_size'])
        mask = {off + j for off, *_ in entries[index][5] for j in range(4)}
        raw = reference.mem[root:root + length]
        signatures.append((index, ' '.join('??' if i in mask else f'{b:02X}' for i, b in enumerate(raw))))
    root = group.BODIES[0][1]
    mask = {off + j for off, *_ in entries[indices[root]][5] for j in range(4)}
    loop_offset, loop_length = 0x14f, 48
    raw = reference.mem[root + loop_offset:root + loop_offset + loop_length]
    loop_signature = ' '.join('??' if i + loop_offset in mask else f'{b:02X}'
                              for i, b in enumerate(raw))
    exported = {'GROUP': group.BODIES[0][1], 'MESH': mesh.BODIES[0][1],
                'TRIANGLES': mesh.BODIES[1][1], 'DRAW': mesh.BODIES[2][1],
                'BEGIN_CALLBACK': old_group[32], 'END_CALLBACK': old_group[33]}
    exported.update({'FX_' + fx.FUNCTIONS[i].name.upper(): fx.FUNCTIONS[i].reference for i in range(8)})
    spans = [nodes[r]['size'] for r in identities]
    codes = [nodes[r]['code'] for r in identities]
    writable = [int(nodes[r]['writable']) for r in identities]
    # Check the whole semantic span on each image, not merely its first byte.
    for i, old in enumerate(identities):
        new = locations[old]
        assert any(s.VirtualAddress <= new and new + spans[i] <= s.VirtualAddress + s.Misc_VirtualSize
                   and bool(s.Characteristics & 0x20000000) == bool(codes[i])
                   and s.Characteristics & 0x40000000
                   and (not writable[i] or s.Characteristics & 0x80000000) for s in target.pe.sections)
        for other in identities[i + 1:]:
            assert new + spans[i] <= locations[other] or locations[other] + nodes[other]['size'] <= new
    return dict(entries=entries, locations=[locations[r] for r in identities], spans=spans,
                code=codes, writable=writable, roots=signatures, loop_signature=loop_signature,
                exports={k: indices[v] for k, v in exported.items()},
                imports=[(indices[r], name) for r, name in imports])


def render(data):
    entries = data['entries']
    lines = ['/* Generated by tools/verify_sites.py --perf-bulk --write. */',
             '#ifndef PF_BULKCONTRACT_TABLE_H', '#define PF_BULKCONTRACT_TABLE_H',
             '#include "nativecontract.h"',
             f'#define BK_BODY_COUNT {len(entries)}u', f'#define BK_SYMBOL_COUNT {len(data["locations"])}u']
    lines += [f'#define BK_{key} {value}u' for key, value in data['exports'].items()]
    for i, (_, signature) in enumerate(data['roots']):
        lines.append(f'#define BK_ROOT_{i}_SIGNATURE "{signature}"')
    lines.append(f'#define BK_LOOP_SIGNATURE "{data["loop_signature"]}"')
    lines.append('static const pf_contract_body bk_bodies[] = {')
    first = 0
    for _, size, _, fnv, crc, patches in entries:
        lines.append(f'    {{{size}, {first}, {len(patches)}, 0x{fnv:08X}u, 0x{crc:08X}u}},')
        first += len(patches)
    lines += ['};', 'static const pf_contract_patch bk_patches[] = {']
    for *_, patches in entries:
        lines += ['    {' + ', '.join(map(str, p)) + '},' for p in patches]
    lines.append('};')
    for key, typename in (('spans', 'unsigned'), ('code', 'unsigned char'), ('writable', 'unsigned char')):
        lines += [f'static const {typename} bk_{key}[] = {{', '    ' + ', '.join(map(str, data[key])), '};']
    lines += ['static const pf_contract_root bk_roots[] = {']
    lines += [f'    {{{index}, BK_ROOT_{i}_SIGNATURE}},' for i, (index, _) in enumerate(data['roots'])]
    lines += ['};', 'static const pf_native_contract bk_contract = {',
              f'    BK_BODY_COUNT, BK_SYMBOL_COUNT, {first}, {len(data["roots"])},',
              '    bk_bodies, bk_patches, bk_code, bk_roots, bk_spans, bk_writable', '};',
              'static const struct { unsigned symbol; const char *name; } bk_imports[] = {']
    lines += [f'    {{{symbol}, "{name}"}},' for symbol, name in data['imports']]
    lines += ['};', '#endif', '']
    return '\n'.join(lines)


def run(write):
    reference = Image(BASELINE)
    content = None
    for key, default in (('AOTR_DAT', 'game.dat'), ('ROTWK_DAT', 'game820.dat')):
        target = Image(Path(os.environ.get(key, 'C:/Jogos/AgeoftheRing/rotwk/' + default)))
        data = graph(reference, target)
        candidate = render(data)
        assert content is None or content == candidate, 'image-specific metadata emitted'
        content = candidate
        print(f'Bulk contract {key}: {len(data["entries"])} hashed regions, {len(data["locations"])} identities')
    header = ROOT / 'perf/src/bulkcontract_table.h'
    if header.exists() and header.read_text(encoding='utf-8') == content:
        print('Runtime bulk table: up to date')
    elif write:
        header.write_text(content, encoding='utf-8', newline='\n')
        print('Runtime bulk table: written')
    else:
        raise AssertionError('runtime bulk table stale; run --perf-bulk --write')
    return 0
