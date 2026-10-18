"""Verify native mesh/final-draw stack contracts without starting the game.

Only verified local bodies are copied into private RAM by the C harness.
The temporary manifest contains numeric offsets, lengths, hashes and
relocation roles, never executable bytes or EA assets.
"""
from __future__ import annotations
import argparse
import contextlib
import hashlib
import io
import struct
import subprocess
import tempfile
from pathlib import Path
import capstone
from capstone import x86
from port_sites import port
from verify_sites import Image
from test_fx_lifecycle import BASELINE, fnv1a

# Research identities in the read-only reference, not install addresses.
BODIES = (
    ('mesh', 0x14A880, 2482), ('triangles', 0x120620, 33),
    ('draw', 0x1203C0, 452), ('geometry_end', 0xB3FD0, 1),
    ('skeleton_set', 0x14D3E7, 30), ('skeleton_clear', 0x14D405, 18),
    ('release', 0x7B724, 16),
)
DEPENDENCIES = (
    ('inverse3', 0x713290), ('multiply3', 0x713540),
    ('array_constructor', 0x1423), ('inverse4', 0x715E90),
    ('look_at', 0x7126B0), ('geometry_begin', 0x18C09B),
    ('get_emissive', 0x13CC80), ('apply_state', 0x11D930),
    ('statistics', 0x129540), ('sorting_draw', 0x11D710),
    ('row_constructor', 0x47A6A9), ('exception_handler', 0x766738),
)
# Each global reference must preserve its offset in one semantic region.
# Kind 0 means mutable non-executable storage; kind 1 is an exact literal.
DATA = (
    ('changed', 0x9EC4F4, 4, 0), ('state', 0x9EE5D8, 0x290, 0),
    ('one', 0x7BB8D8, 4, 1), ('sas', 0x9F36B4, 4, 0),
    ('limit', 0x9EDA80, 4, 0), ('emissive_mode', 0x9EC3D8, 1, 0),
    ('enabled', 0x9B5FCD, 1, 0), ('snapshot', 0x9EC3FD, 1, 0),
    ('device', 0x9EDA34, 4, 0), ('draw_count', 0x9EDA6C, 4, 0),
    ('api_count', 0x9EDA98, 4, 0), ('threshold', 0x7C2424, 4, 1),
)
GLOBALS = len(BODIES)+len(DEPENDENCIES)
TABLES = GLOBALS+len(DATA)


def instructions(image, body, rva):
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.detail=True
    result=list(md.disasm(bytes(image.mem[rva:rva+BODIES[body][2]]),image.base+rva))
    assert sum(i.size for i in result)==BODIES[body][2], 'partial body'
    return result


def recipe(reference, body):
    rva=BODIES[body][1]; patches=[]; starts=set(); branches=[]
    functions=[x[1] for x in BODIES+DEPENDENCIES]
    for ins in instructions(reference,body,rva):
        off=ins.address-reference.base-rva; starts.add(off)
        branch=ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)
        if branch and ins.operands[0].type==x86.X86_OP_IMM:
            dest=ins.operands[0].imm-reference.base
            if rva<=dest<rva+BODIES[body][2]: branches.append(dest-rva)
            else:
                assert ins.size==5 and ins.bytes[0] in (0xE8,0xE9)
                assert dest in functions, ('unknown direct dependency',hex(dest))
                patches.append((off+1,0,functions.index(dest),0))
        fields=[]
        if ins.disp_size==4 and reference.base<=(ins.disp&0xffffffff)<reference.base+len(reference.mem):
            fields.append(off+ins.disp_offset)
        if not branch and ins.imm_size==4 and any(op.type==x86.X86_OP_IMM and reference.base<=op.imm<reference.base+len(reference.mem) for op in ins.operands):
            fields.append(off+ins.imm_offset)
        for field in fields:
            dest=struct.unpack_from('<I',reference.mem,rva+field)[0]-reference.base
            if dest in functions: symbol,add=functions.index(dest),0
            elif 0x120584<=dest<0x1205A4: symbol,add=TABLES,dest-0x120584
            else:
                matches=[(GLOBALS+i,dest-start) for i,(_,start,size,_) in enumerate(DATA) if start<=dest<start+size]
                assert len(matches)==1, ('unknown data role',hex(dest))
                symbol,add=matches[0]
            patches.append((field,1,symbol,add))
    assert all(b in starts for b in branches)
    return patches


def verify(reference,target):
    functions=[]
    for index,(name,rva,*size) in enumerate(BODIES+DEPENDENCIES):
        # Folded tiny bodies and the per-function exception handler cannot
        # be located by a unique standalone prefix. Resolve their exact
        # role from the already unique complete mesh body instead.
        if name in ('geometry_end','release','row_constructor','exception_handler'):
            field={'geometry_end':0x93F,'release':0x97A,'row_constructor':0x2FA,'exception_handler':3}[name]
            absolute=name in ('row_constructor','exception_handler')
            value=struct.unpack_from('<I' if absolute else '<i',target.mem,functions[0]+field)[0]
            found=value-target.base if absolute else functions[0]+field+4+value
            assert any(lo<=found<hi for lo,hi in target.exec_ranges), name
            if name=='row_constructor': assert bytes(target.mem[found:found+3])==b'\x8B\xC1\xC3'
            functions.append(found)
            continue
        length=max(24,size[0]) if size else 0
        _,hits=port(reference,rva,target,lengths=(length,) if length else (24,32,48,64,96,128))
        assert len(hits)==1, f'{name}: not unique'
        functions.append(hits[0])
    symbols=functions+[None]*len(DATA)+[functions[2]+452]
    records=[]
    for index,(_,old,size) in enumerate(BODIES):
        new=functions[index]; patches=recipe(reference,index)
        before=bytearray(reference.mem[old:old+size]); after=bytearray(target.mem[new:new+size])
        # Exact instruction bytes after relocation masking preserve every
        # stack operand, branch, call slot and immediate constant.
        for field,kind,symbol,add in patches:
            value=struct.unpack_from('<I' if kind else '<i',after,field)[0]
            dest=value-target.base if kind else new+field+4+value
            expected=dest-add
            if symbols[symbol] is None: symbols[symbol]=expected
            assert symbols[symbol]==expected, 'inconsistent call/data role'
            before[field:field+4]=after[field:field+4]=b'\0'*4
        assert before==after, f'{BODIES[index][0]}: full body differs'
        instructions(target,index,new)
        raw=bytes(target.mem[new:new+size])
        records.append((new,size,fnv1a(raw),patches))
        print(f'{BODIES[index][0]}: {size} bytes SHA256 {hashlib.sha256(raw).hexdigest()}',flush=True)
    assert all(x is not None for x in symbols)
    for i,(_,old,size,kind) in enumerate(DATA):
        new=symbols[GLOBALS+i]
        assert any(s.VirtualAddress<=new and new+size<=s.VirtualAddress+s.Misc_VirtualSize and not s.Characteristics&0x20000000 and (kind or s.Characteristics&0x80000000) for s in target.pe.sections), 'invalid data region'
        if kind: assert bytes(reference.mem[old:old+size])==bytes(target.mem[new:new+size])
    assert len(set(symbols))==len(symbols), 'aliased roles'
    table=[]
    draw_starts={i.address-target.base-functions[2] for i in instructions(target,2,functions[2])}
    for offset in range(0,32,4):
        old=struct.unpack_from('<I',reference.mem,0x120584+offset)[0]-reference.base-BODIES[2][1]
        new=struct.unpack_from('<I',target.mem,symbols[TABLES]+offset)[0]-target.base-functions[2]
        assert old==new and new in draw_starts, 'changed switch target'
        table.append(new)
    return records,table,symbols


def refusals(reference,target,symbols):
    mesh,tri,draw=symbols[:3]
    mutations=[(mesh+0x19,b'\x01'), # stack allocation
               (mesh+0x92B,struct.pack('<i',symbols[12]-(mesh+0x92F))),
               (mesh+0x9B0,b'\x0C'), # RET 12
               (tri+0x1F,b'\x14'), # caller cleanup
               (draw+0x195,b'\x90'), # direct DIP epilogue
               (symbols[5]+0x10,b'\x1C'), # skeleton clear field
               (symbols[6]+8,b'\x08'), # reference field
               (symbols[TABLES],struct.pack('<I',target.base+draw+0x1C3)),
               (symbols[GLOBALS+2],b'\0\0\0\0'),
               (mesh+0x2C9,struct.pack('<I',target.base+symbols[GLOBALS])),
               (target.text_end-4096,bytes(target.mem[mesh:mesh+2482]))]
    for rva,replacement in mutations:
        old=bytes(target.mem[rva:rva+len(replacement)]); target.mem[rva:rva+len(replacement)]=replacement
        try:
            with contextlib.redirect_stdout(io.StringIO()): verify(reference,target)
        except AssertionError: pass
        else: raise AssertionError(f'altered mesh/draw contract accepted at {rva:#x}')
        finally: target.mem[rva:rva+len(old)]=old
    print(f'Native mesh/draw: {len(mutations)} refusal controls passed',flush=True)


def write_manifest(path,records,table):
    """Numeric relocation metadata only; never write executable bytes."""
    lines=[]
    for rva,size,digest,patches in records:
        lines.append(f'{rva} {size} {digest} {len(patches)}')
        lines.extend(' '.join(map(str,p)) for p in patches)
    lines.append(' '.join(map(str,table)))
    path.write_text('\n'.join(lines)+'\n',encoding='ascii')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image',type=Path); parser.add_argument('--self-test',action='store_true')
    parser.add_argument('--execute',action='store_true'); args=parser.parse_args()
    reference,target=Image(BASELINE),Image(args.image)
    records,table,symbols=verify(reference,target)
    if args.self_test: refusals(reference,target,symbols)
    if args.execute:
        with tempfile.TemporaryDirectory(prefix='sage-mesh-draw-') as folder:
            path=Path(folder)/'contract.txt'
            write_manifest(path,records,table)
            exe=Path(__file__).resolve().parent.parent/'perf/build/test_native_mesh.exe'
            command=[str(exe),str(args.image),str(path)]
            result=subprocess.call(command)
            if result: return result
            if args.self_test:
                for fault in (1,2):
                    control=subprocess.run(command+[str(fault)],capture_output=True,text=True)
                    assert control.returncode==1 and 'native mesh refused' in control.stderr, control
                print('Native mesh: weak recursive gate and wrong stack-distance controls rejected',flush=True)
    return 0

if __name__=='__main__': raise SystemExit(main())
