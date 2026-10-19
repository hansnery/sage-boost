"""Generate only our experimental palette-texture shaders into perf/build/."""
import argparse
from pathlib import Path
import re
from inst_shaders import (Compiler, DEFAULT_BIG, HLSL, classify, fnv1a,
                          parse, read_fx, strip_comments, vertex_shaders)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--big', default=DEFAULT_BIG)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = Path(HLSL).read_text()
    declaration = 'float4 IR[IR_COUNT] : register(IR_REG);'
    assert source.count(declaration) == 1
    source = source.replace(declaration, '''sampler2D paletteTexture : register(s0);
float4 paletteParams : register(IR_REG);
float4 readPalette(float index) {
    return tex2Dlod(paletteTexture,
        float4((floor(index) + 0.5) * paletteParams.x, paletteParams.z, 0, 0));
}''')
    source, replaced = re.subn(r'IR\[([^\]]+)\]', r'readPalette(\1)', source)
    assert replaced == 7 and 'IR[' not in source
    compiler = Compiler()
    selected = []
    for index, ea_code in vertex_shaders(read_fx(args.big)):
        if index not in [5, 476]:
            continue
        info = classify(*parse(compiler.disasm(ea_code)))
        assert isinstance(info, dict) and info['joints'] == 1
        lo, hi = info['ir']
        defines = {'FAMILY': info['family'], 'JOINTS': info['joints'],
                   'MAP0': info['map0'], 'MAP1': info['map1'],
                   'IR_REG': f'c{lo}', 'IR_COUNT': hi-lo}
        for role, reg in info['roles'].items():
            if role not in ('BONES', 'WORLD'):
                defines[f'R_{role}'] = f'c{reg}'
        code = compiler.compile(source.encode(), defines, target='vs_3_0')
        disassembly = compiler.disasm(code)
        assert 'texldl' in disassembly and f'paletteParams' in disassembly
        selected.append((index, fnv1a(ea_code), strip_comments(code)))
    assert [x[0] for x in selected] == [5, 476]
    lines = ['/* Generated from our inst.hlsl; contains no EA shader bytecode. */']
    for index, identity, words in selected:
        lines.append(f'static const unsigned palette_identity_{index} = 0x{identity:08x}u;')
        lines.append(f'static const DWORD palette_code_{index}[] = {{')
        for start in range(0, len(words), 8):
            lines.append('    ' + ','.join(f'0x{word:08x}u' for word in words[start:start+8]) + ',')
        lines.append('};')
    args.output.write_text('\n'.join(lines) + '\n')
    print('palette shaders:', [(index, len(words)*4) for index, _, words in selected])


if __name__ == '__main__':
    main()
