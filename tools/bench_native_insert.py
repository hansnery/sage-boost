"""Verify and screen native rendering-task insertion without starting the game.

Native code is read only into private process RAM. The manifest contains
numeric relocation roles and hashes. Heap callbacks are controlled; exception handling is a
fail-stop dependency; this screen qualifies normal control flow only.
"""
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
from test_fx_lifecycle import BASELINE, fnv1a
from verify_sites import Image
from port_sites import port

# Research identities, never installed addresses. Native queue selection,
# insertion, growth, erase, record ownership and array helpers.
BODIES = (
    (0x174AAD,249),(0x629188,31),(0x174A8A,35),(0x72A94,47),
    (0x1736D6,62),(0x17480C,100),(0x7B724,16),(0x2A79A5,4),
    (0x629512,98),(0x173FB6,51),(0x17422D,264),(0x17330A,71),
    (0x628FA4,59),(0x628FDF,17),(0x6290B2,94),(0x17396F,29),
    (0x173AE0,25),(0x173731,85),(0x17398C,38),(0x173786,29),
    (0x1737A3,29),(0x1739B2,27),(0x173FE9,180),(0x629110,96),
    (0x1737C0,47),(0x7E380,20),(0x6299C4,102),(0x1737EF,45),
    (0x1734D1,47),(0x173499,56),(0x17381C,37),(0x3F74B3,28),
    (0x173E1E,30),
)
INDEX = {r:i for i,(r,_) in enumerate(BODIES)}
EXCEPTIONS = {0x767DCB,0x767C48,0x767C72,0x767CAA,0x767C60,
              0x87C768,0x87C738,0x87C748,0x87C778,0x628FF0,0x629BC0}
# Controlled successful allocation and release only; no game heap is loaded.
EXTERNALS = {0x307F0:len(BODIES)+1,0x30830:len(BODIES)+2}
ROOTS = (0x174AAD,)


def recipe(image, rva, size):
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
    patches=[];starts=set();jumps=[];total=0
    raw=bytes(image.mem[rva:rva+size])
    for ins in md.disasm(raw,image.base+rva):
        off=ins.address-image.base-rva;starts.add(off);total+=ins.size
        assert ins.mnemonic not in ('fldcw','fnstcw','ldmxcsr','stmxcsr')
        branch=ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP)
        if branch and ins.operands[0].type==x86.X86_OP_IMM:
            dest=ins.operands[0].imm-image.base
            if rva<=dest<rva+size:
                jumps.append(dest-rva)
            else:
                assert ins.imm_size==4 and ins.bytes[0] in (0xE8,0xE9)
                patches.append((off+ins.imm_offset,0,dest))
        if not branch and ins.imm_size==4:
            value=struct.unpack_from('<I',raw,off+ins.imm_offset)[0]
            if image.base<=value<image.base+len(image.mem):
                patches.append((off+ins.imm_offset,1,value-image.base))
        for operand in ins.operands:
            if operand.type==x86.X86_OP_MEM:
                assert operand.mem.base or operand.mem.segment==x86.X86_REG_FS,'unexpected absolute memory'
                assert operand.mem.segment in (0,x86.X86_REG_FS,x86.X86_REG_ES)
    assert total==size and all(j in starts for j in jumps),'body/branch boundary'
    normalized=bytearray(raw)
    for off,_,_ in patches:normalized[off:off+4]=b'\0'*4
    return bytes(normalized),patches


def verify(reference,target):
    locations={};pending=[];records=[None]*len(BODIES)
    for rva in ROOTS:
        _,hits=port(reference,rva,target,lengths=(BODIES[INDEX[rva]][1],))
        assert len(hits)==1,('insertion root not unique',hex(rva))
        locations[rva]=hits[0];pending.append(rva)
    while pending:
        old_rva=pending.pop(0);index=INDEX[old_rva]
        if records[index] is not None:continue
        rva=locations[old_rva];size=BODIES[index][1]
        old,old_patches=recipe(reference,old_rva,size)
        new,new_patches=recipe(target,rva,size)
        assert old==new and len(old_patches)==len(new_patches),('complete insertion body differs',hex(old_rva))
        patches=[]
        for (off,kind,dependency),(off2,kind2,destination) in zip(old_patches,new_patches):
            assert (off,kind)==(off2,kind2)
            assert dependency in INDEX or dependency in EXCEPTIONS or dependency in EXTERNALS,('unexpected dependency',hex(dependency))
            if dependency in locations:assert locations[dependency]==destination,'dependency identity differs'
            else:locations[dependency]=destination
            if dependency in INDEX:
                pending.append(dependency);symbol=INDEX[dependency]
            else:symbol=EXTERNALS.get(dependency,len(BODIES))
            patches.append((off,kind,symbol))
        raw=bytes(target.mem[rva:rva+size])
        records[index]=(rva,size,fnv1a(raw),patches)
    assert all(r is not None for r in records),'incomplete native closure'
    for old,located in locations.items():
        assert any(s.VirtualAddress<=located<s.VirtualAddress+s.Misc_VirtualSize
                   and bool(s.Characteristics&0x20000000)==(old not in (0x87C778,0x87C738,0x87C748,0x87C768))
                   for s in target.pe.sections),'dependency section role'
    print('NATIVE_INSERT',len(records),'bodies',sum(r[1] for r in records),'bytes',flush=True)
    return records


def refusals(reference,target,records):
    # Method limit, mesh-model field, capacity branch, record store and
    # allocator element-size conversion. Refuse before native execution.
    controls=((0,0x23),(0,0x40),(10,0x25),(29,0x15),(31,0x8))
    refused=0
    for index,off in controls:
        rva=records[index][0];old=target.mem[rva+off];target.mem[rva+off]=old^1
        try:
            with contextlib.redirect_stdout(io.StringIO()):verify(reference,target)
        except (AssertionError,ValueError,IndexError):refused+=1
        finally:target.mem[rva+off]=old
    assert refused==len(controls)
    print('Five altered insertion contracts refused',flush=True)


def write_manifest(path,records):
    lines=[str(len(records))]
    for rva,size,digest,patches in records:
        lines.append(f'{rva} {size} {digest} {len(patches)}')
        lines.extend(' '.join(map(str,p)) for p in patches)
    path.write_bytes(('\n'.join(lines)+'\n').encode())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('image',type=Path);p.add_argument('executable',type=Path)
    p.add_argument('--samples',type=int,default=21);p.add_argument('--self-test',action='store_true')
    a=p.parse_args();assert 0<=a.samples<=101
    reference,target=Image(BASELINE),Image(a.image);records=verify(reference,target)
    if a.self_test:refusals(reference,target,records)
    print('image_sha256',hashlib.sha256(a.image.read_bytes()).hexdigest(),flush=True)
    print('exe_sha256',hashlib.sha256(a.executable.read_bytes()).hexdigest(),flush=True)
    with tempfile.TemporaryDirectory(prefix='sage-native-insert-') as folder:
        path=Path(folder)/'manifest.txt';write_manifest(path,records)
        command=[str(a.executable.resolve()),str(a.image.resolve()),str(path)]
        result=subprocess.call(command+[str(a.samples),'0'])
        if result:return result
        if a.self_test:
            bad=subprocess.run(command+['0','1'],capture_output=True,text=True)
            assert bad.returncode==1 and 'insert oracle mismatch' in bad.stderr,bad
            print('Oracle rejects corrupted inserted record',flush=True)
    return 0


if __name__=='__main__':raise SystemExit(main())
