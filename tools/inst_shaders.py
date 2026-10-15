"""Dev-time generator (not shipped): perf/src/inst_shaders.h.

Reads the user's local rotwk\\Shaders.big, finds every vertex shader in
shaders\\compiled\\defaultw3d.fxo, works out which of our instanced variants
replaces it (family, skinning, texture mappings, the register of every
constant it reads, the registers it leaves free), compiles
perf/shaders/inst.hlsl for it with Windows' d3dcompiler_47.dll, and writes
a C table: a hash of the game's bytecode (to recognise it at run time),
our bytecode, and the instance register range. No game bytecode is written.

    python tools/inst_shaders.py [--big PATH] [--only N,N,...] [--skip N,N,...]
"""
import argparse
import ctypes
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HLSL = os.path.join(ROOT, 'perf', 'shaders', 'inst.hlsl')
OUT = os.path.join(ROOT, 'perf', 'src', 'inst_shaders.h')
DEFAULT_BIG = r'C:\Jogos\AgeoftheRing\rotwk\Shaders.big'
FX_NAME = 'shaders\\compiled\\defaultw3d.fxo'
# Only its low-quality lit shaders (family 5): the others need the tangent frame.
NM_NAME = 'shaders\\compiled\\normalmapped.fxo'
MAX_CONST = 256

# Constant roles, shared with the GPU test (which sets them sensibly).
ROLES = ['VP', 'RCPRANGE', 'AMB', 'OPAC', 'FOGRCP', 'DL', 'FOG', 'VIEWI', 'PL', 'CD', 'CS',
         'SHIN', 'SHROUD', 'HCE', 'MAP0OFF', 'TT0', 'MAP1OFF', 'TT1', 'SI', 'CLOUD',
         'CLOUDOFF', 'TT0OFF', 'TAOFF', 'BMFLAG', 'VIEW', 'PROJ', 'BONES', 'WORLD', 'DIFF',
         'OPACOV']
CTAB_ROLE = {'DirectionalLight': 'DL', 'ShadowInfo': 'SI', 'Fog': 'FOG', 'ViewI': 'VIEWI',
             'PointLight': 'PL', 'ColorDiffuse': 'CD', 'ColorSpecular': 'CS',
             'Shininess': 'SHIN', 'Shroud': 'SHROUD', 'Cloud': 'CLOUD',
             'HouseColorEnable': 'HCE', 'View': 'VIEW', 'Projection': 'PROJ',
             'DiffuseColor': 'DIFF', 'OpacityOverride': 'OPACOV'}


def big_entries(path):
    with open(path, 'rb') as f:
        head = f.read(16)
        if head[:4] not in (b'BIGF', b'BIG4'):
            raise SystemExit(f'{path}: not a BIG archive')
        count = struct.unpack('>I', head[8:12])[0]
        out = {}
        for _ in range(count):
            off, size = struct.unpack('>II', f.read(8))
            name = bytearray()
            while True:
                c = f.read(1)
                if c in (b'', b'\0'):
                    break
                name += c
            out[name.decode('latin1').lower()] = (off, size)
        return out


def read_fx(big, name=FX_NAME):
    off, size = big_entries(big)[name]
    with open(big, 'rb') as f:
        f.seek(off)
        return f.read(size)


def vertex_shaders(fx):
    """(index among all shaders, bytes) of each vertex shader blob."""
    out, i, n = [], 4, 0
    while i + 8 <= len(fx):
        tok = struct.unpack_from('<I', fx, i)[0]
        if tok >> 16 in (0xFFFE, 0xFFFF) and tok & 0xFFFF in (0x0101, 0x0200, 0x0201, 0x0300):
            size = struct.unpack_from('<I', fx, i - 4)[0]
            if 8 <= size <= len(fx) - i and struct.unpack_from('<I', fx, i + size - 4)[0] == 0xFFFF:
                if tok >> 16 == 0xFFFE:
                    out.append((n, fx[i:i + size]))
                n += 1
                i += size
                continue
        i += 4
    return out


def fnv1a(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


class Compiler:
    def __init__(self):
        self.dll = ctypes.WinDLL('d3dcompiler_47.dll')

    @staticmethod
    def _blob(p):
        vt = ctypes.cast(ctypes.cast(p, ctypes.POINTER(ctypes.c_void_p))[0],
                         ctypes.POINTER(ctypes.c_void_p))
        ptr = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vt[3])(p)
        size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vt[4])(p)
        data = ctypes.string_at(ptr, size)
        ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(vt[2])(p)
        return data

    def disasm(self, code):
        out = ctypes.c_void_p()
        hr = self.dll.D3DDisassemble(ctypes.c_char_p(code), ctypes.c_size_t(len(code)), 0, None,
                                     ctypes.byref(out))
        if hr != 0:
            raise RuntimeError(f'D3DDisassemble {hr & 0xFFFFFFFF:08X}')
        return self._blob(out).decode('latin1')

    def compile(self, src, defines, target='vs_2_0'):
        class Macro(ctypes.Structure):
            _fields_ = [('Name', ctypes.c_char_p), ('Definition', ctypes.c_char_p)]
        macros = (Macro * (len(defines) + 1))()
        keep = []
        for i, (k, v) in enumerate(defines.items()):
            keep += [k.encode(), str(v).encode()]
            macros[i].Name, macros[i].Definition = keep[-2], keep[-1]
        code, err = ctypes.c_void_p(), ctypes.c_void_p()
        hr = self.dll.D3DCompile(ctypes.c_char_p(src), ctypes.c_size_t(len(src)), b'inst.hlsl',
                                 macros, None, b'main', target.encode(), 1 << 15, 0,
                                 ctypes.byref(code), ctypes.byref(err))
        msg = self._blob(err).decode('latin1') if err.value else ''
        if hr != 0:
            raise RuntimeError(f'D3DCompile {hr & 0xFFFFFFFF:08X}: {msg}')
        return self._blob(code)


def strip_comments(code):
    """Bytecode without comment blocks (the constant table, the creator)."""
    toks = struct.unpack(f'<{len(code) // 4}I', code)
    out, i = [toks[0]], 1
    while i < len(toks):
        t = toks[i]
        if t & 0xFFFF == 0xFFFE:
            i += 1 + ((t >> 16) & 0x7FFF)
            continue
        out.append(t)
        i += 1
    return out


def registers(table):
    return {m.group(1): (int(m.group(2)), int(m.group(3)))
            for m in re.finditer(r'//\s+(\w+)\s+c(\d+)\s+(\d+)', table)}


def parse(text):
    """Constant tables, preshader outputs in register order, input semantics."""
    head, _, body = text.partition('\n    vs_')
    tables = re.findall(r'// Registers:\n//\n(.*?)\n//\n', head, re.S)
    vs_regs = registers(tables[-1]) if tables else {}
    pre_in = registers(tables[0]) if len(tables) > 1 else {}
    pre_out = {}
    if 'preshader' in head:
        pre = head.split('preshader', 1)[1].split('// Generated', 1)[0]
        for line in pre.splitlines():
            m = re.match(r'\s+(\w+)\s+c(\d+)(\.\w+)?,', line)
            if m:
                pre_out[int(m.group(2))] = line.strip()
    inputs = re.findall(r'dcl_(\w+) v\d+', body)
    return vs_regs, pre_in, pre_out, inputs


def classify(vs_regs, pre_in, pre_out, inputs, normal_mapped=False):
    """Defines, roles and the instance range, or a reason string."""
    names = set(vs_regs)
    if normal_mapped:
        if 'tangent' in inputs or 'binormal' in inputs:
            return 'normal-mapped (tangent frame)'
        if not ('DiffuseColor' in names and 'DirectionalLight' in names and 'Fog' in names):
            return 'not a unit family'
        family = 5
    elif 'ShadowInfo' in names:
        family = 1
    elif 'DirectionalLight' in names:
        family = 2 if 'Fog' in names else 4
    elif 'View' in names and 'Projection' in names:
        family = 3
    else:
        return 'not a unit family'
    bones = vs_regs.get('WorldBones') or vs_regs.get('WorldBones_L')
    if bones:
        joints = 2 if 'blendweight' in inputs or 'position1' in inputs else 1
    elif 'World' in names:
        joints = 0
    else:
        return 'no bones and no world matrix'
    map0 = 1 if 'TexCoordTransform_0' in names else 0
    if family != 3 and 'TextureAnimation_FPS_NumPerRow_LastFrame_FrameOffset_0' in pre_in:
        map0 = 2 if not map0 else -1
    map1 = 1 if 'TexCoordTransform_1' in names else 0
    if map0 < 0:
        return 'two mappings on texture 0'
    if family == 5 and (map0 or map1):
        return 'texture mappings at low shader quality'
    if family == 5 and 'color' in inputs:
        return 'low normal-mapped with vertex colour'

    roles = {}
    for name, (reg, size) in vs_regs.items():
        if name in CTAB_ROLE:
            roles[CTAB_ROLE[name]] = reg
    if map0 == 1:
        roles['TT0'] = vs_regs['TexCoordTransform_0'][0]
    if map1:
        roles['TT1'] = vs_regs['TexCoordTransform_1'][0]
    if family == 3 and 'TexCoordTransform_0' in names:
        roles['TT0'] = vs_regs['TexCoordTransform_0'][0]

    outs = sorted(pre_out)
    if family == 5:
        want = ['VP0', 'VP1', 'VP2', 'VP3', 'RCPRANGE', 'AMB', 'FOGRCP']
        if len(outs) != len(want) or 'OPACOV' not in roles:
            return f'low normal-mapped preshader outputs {outs}'
        for role, reg in zip(want, outs):
            roles[role] = reg
        if [roles['VP0'] + i for i in range(4)] != outs[:4]:
            return 'view-projection not in 4 consecutive registers'
        roles['VP'] = roles.pop('VP0')
        for i in (1, 2, 3):
            roles.pop(f'VP{i}')
        checks = {'RCPRANGE': r'^rcp c\d+\.x, c\d+\.x$', 'AMB': r'^mul c\d+\.xyz, c\d+\.xyz, c\d+\.xyz$',
                  'FOGRCP': r'^rcp c\d+\.x, r0\.w$'}
    elif family == 3:
        if len(outs) != 4 or 'TT0' not in roles:
            return f'shadow-map preshader outputs {outs}'
        roles['OPAC'], roles['TT0OFF'], roles['TAOFF'], roles['BMFLAG'] = outs
        checks = {'OPAC': r'^mul c\d+\.x,', 'TT0OFF': r'^mul c\d+\.xy,', 'TAOFF': r'^mul c\d+\.xy,',
                  'BMFLAG': r'^ge c\d+\.x,'}
    else:
        want = ['VP0', 'VP1', 'VP2', 'VP3', 'RCPRANGE', 'AMB', 'OPAC']
        if map0:
            want.append('MAP0OFF')
        if map1:
            want.append('MAP1OFF')
        if family == 1:
            want.append('CLOUDOFF')
        if family != 4:
            want.append('FOGRCP')
        if len(outs) != len(want):
            return f'preshader outputs {outs}, expected {want}'
        for role, reg in zip(want, outs):
            roles[role] = reg
        if [roles['VP0'] + i for i in range(4)] != outs[:4]:
            return 'view-projection not in 4 consecutive registers'
        roles['VP'] = roles.pop('VP0')
        for i in (1, 2, 3):
            roles.pop(f'VP{i}')
        checks = {'RCPRANGE': r'^rcp c\d+\.x, c\d+\.x$', 'AMB': r'^add c\d+\.xyz,',
                  'OPAC': r'^mul c\d+\.x,'}
        if family != 4:
            checks['FOGRCP'] = r'^rcp c\d+\.x, r0\.w$'
        if family == 1:
            checks['CLOUDOFF'] = r'^frc c\d+\.xy,'
        if map0 == 1:
            checks['MAP0OFF'] = r'^mul c\d+\.xy,'
        if map0 == 2:
            checks['MAP0OFF'] = r'^mul c\d+\.xy, r2\.w,'
        if map1:
            checks['MAP1OFF'] = r'^mul c\d+\.xy,'
    for role, rx in checks.items():
        if not re.match(rx, pre_out[roles[role]]):
            return f'{role} preshader line "{pre_out[roles[role]]}"'

    used = set()
    for name, (reg, size) in vs_regs.items():
        if name not in ('WorldBones', 'WorldBones_L', 'World'):
            used.update(range(reg, reg + size))
    used.update(outs)
    best, start = (0, 0), None
    for r in range(MAX_CONST + 1):
        if r < MAX_CONST and r not in used:
            start = r if start is None else start
        elif start is not None:
            if r - start > best[1] - best[0]:
                best = (start, r)
            start = None
    if bones:
        roles['BONES'] = bones[0]
    else:
        roles['WORLD'] = vs_regs['World'][0]
    return {'family': family, 'joints': joints, 'map0': map0, 'map1': map1, 'roles': roles,
            'ir': best, 'ea_bones': bones[1] // 2 if bones else 0}


NAMES = {1: 'lit+shadow', 2: 'lit', 3: 'shadowmap', 4: 'lit low', 5: 'normalmapped low'}
# normalmapped.fxo's shader n is numbered NM_BASE + n in the table and the logs.
NM_BASE = 1000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--big', default=DEFAULT_BIG)
    ap.add_argument('--skip', default='', help='EA shader indices to leave out (failed the GPU test)')
    args = ap.parse_args()
    skip = {int(x) for x in args.skip.split(',') if x}

    cc = Compiler()
    src = open(HLSL, 'rb').read()
    variants, refused = [], []
    shaders = [(n, blob, False) for n, blob in vertex_shaders(read_fx(args.big))]
    shaders += [(NM_BASE + n, blob, True) for n, blob in vertex_shaders(read_fx(args.big, NM_NAME))]
    for n, blob, nm in shaders:
        text = cc.disasm(blob)
        version = re.search(r'\n    (vs_\d_\d)', text).group(1)
        info = classify(*parse(text), normal_mapped=nm)
        if isinstance(info, str):
            refused.append((n, version, info))
            continue
        if n in skip:
            refused.append((n, version, 'failed the GPU test'))
            continue
        r = info['roles']
        lo, hi = info['ir']
        defines = {'FAMILY': info['family'], 'JOINTS': info['joints'], 'MAP0': info['map0'],
                   'MAP1': info['map1'], 'IR_REG': f'c{lo}', 'IR_COUNT': hi - lo}
        for role, reg in r.items():
            if role not in ('BONES', 'WORLD'):
                defines[f'R_{role}'] = f'c{reg}'
        try:
            code = cc.compile(src, defines)
        except RuntimeError as e:
            refused.append((n, version, str(e).splitlines()[0]))
            continue
        ours = cc.disasm(code)
        m = re.search(r'//\s+IR\s+c(\d+)\s+(\d+)', ours)
        if not m or int(m.group(1)) != lo:
            refused.append((n, version, f'our IR landed at {m.group(0) if m else "?"}'))
            continue
        name = f"{NAMES[info['family']]} {info['joints']}j"
        name += {0: '', 1: ' tt0', 2: ' anim0'}[info['map0']] + (' tt1' if info['map1'] else '')
        variants.append({'n': n, 'name': name, 'hash': fnv1a(blob), 'size': len(blob),
                         'code': strip_comments(code), 'info': info, 'version': version})

    lines = ['/* Generated by tools/inst_shaders.py from perf/shaders/inst.hlsl. Do not edit.',
             ' * Each entry: the FNV-1a hash and size of a game vertex shader (to recognise',
             ' * it; the game\'s bytecode is not included), and our instanced replacement. */',
             '#ifndef PF_INST_SHADERS_H', '#define PF_INST_SHADERS_H', '',
             'enum { ' + ', '.join(f'PF_ROLE_{x}' for x in ROLES) + ', PF_ROLE_COUNT };', '',
             'typedef struct {',
             '    unsigned ea_hash, ea_size; /* the game\'s variant */',
             '    unsigned char family;      /* 1 lit + shadow map, 2 lit, 3 shadow map, 4 lit low,',
             '                                * 5 normal-mapped material at low quality */',
             '    unsigned char joints;      /* 0 rigid (3 registers per instance), 1-2 skinned */',
             '    unsigned char map0, map1;',
             '    unsigned short ir_reg, ir_count; /* instance registers */',
             '    unsigned short ea_bones;   /* the game\'s bone palette */',
             '    unsigned short ea_index;   /* the shader\'s index in defaultw3d.fxo, or 1000 +',
             '                                * its index in normalmapped.fxo */',
             '    const unsigned *code;      /* our vs_2_0 bytecode */',
             '    unsigned code_dwords;',
             '    const char *name;',
             '    short roles[PF_ROLE_COUNT]; /* register of each constant, -1 if unused */',
             '} pf_inst_variant;', '']
    for v in variants:
        body = ', '.join(f'0x{t:08X}' for t in v['code'])
        lines.append(f"static const unsigned pf_inst_code_{v['n']}[] = {{ {body} }};")
    lines += ['', f'#define PF_INST_VARIANTS {len(variants)}', '',
              'static const pf_inst_variant pf_inst_variants[PF_INST_VARIANTS] = {']
    for v in variants:
        i = v['info']
        roles = ', '.join(str(i['roles'].get(x, -1)) for x in ROLES)
        lines.append(f"    {{ 0x{v['hash']:08X}u, {v['size']}u, {i['family']}, {i['joints']}, "
                     f"{i['map0']}, {i['map1']}, {i['ir'][0]}, {i['ir'][1] - i['ir'][0]}, "
                     f"{i['ea_bones']}, {v['n']}, pf_inst_code_{v['n']}, {len(v['code'])}, "
                     f"\"{v['name']}\", {{ {roles} }} }},")
    lines += ['};', '', '#endif', '']
    with open(OUT, 'w', newline='\n') as f:
        f.write('\n'.join(lines))

    for v in variants:
        i = v['info']
        print(f"{v['n']:4d} {v['version']} {v['name']:28s} ir c{i['ir'][0]}..c{i['ir'][1] - 1} "
              f"({i['ir'][1] - i['ir'][0]}) hash {v['hash']:08X} ours {len(v['code']) * 4} B")
    print(f'{len(variants)} variants written to {os.path.relpath(OUT, ROOT)}')
    for n, version, why in refused:
        print(f'refused {n:4d} {version}: {why}')


if __name__ == '__main__':
    sys.exit(main())
