"""Verify and screen native rendering-task sorting without starting the game.

Native code is read only into private process RAM. The manifest contains
numeric relocation roles and hashes. Exception handling is deliberately a
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

# Research identities, never installed addresses. Includes native owning
# record operations, array-copy/destruction helpers and both comparators.
BODIES = (
    (0x72A94,47),(0x7B724,16),(0x7E380,20),(0x17330A,71),
    (0x173499,56),(0x1734D1,47),(0x17351C,86),(0x173572,135),
    (0x17368A,29),(0x1736A7,47),(0x173731,85),(0x173786,29),
    (0x173841,46),(0x173878,75),(0x1738C3,172),(0x1739CD,23),
    (0x1739E9,92),(0x173A45,27),(0x173A60,83),(0x173AB3,45),
    (0x173AF9,101),(0x173B5E,118),(0x173BD4,25),(0x173BED,23),
    (0x173E3C,57),(0x173E75,58),(0x17409D,74),(0x1740E7,100),
    (0x174335,27),(0x1746C3,144),(0x174870,67),(0x4C6F77,89),
    (0x628FA4,59),(0x628FDF,17),(0x6290B2,94),(0x629110,96),
    (0x629188,31),(0x6299C4,102),
)
INDEX = {r:i for i,(r,_) in enumerate(BODIES)}
EXCEPTIONS = {0x767C48,0x767CBC,0x767CAA,0x767C84,0x767C72,
              0x87C778,0x87C738,0x87C748,0x628FF0,0x629BC0}
ROOTS = (0x174870,0x17368A,0x1736A7)


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
        assert len(hits)==1,('sort root not unique',hex(rva))
        locations[rva]=hits[0];pending.append(rva)
    while pending:
        old_rva=pending.pop(0);index=INDEX[old_rva]
        if records[index] is not None:continue
        rva=locations[old_rva];size=BODIES[index][1]
        old,old_patches=recipe(reference,old_rva,size)
        new,new_patches=recipe(target,rva,size)
        assert old==new and len(old_patches)==len(new_patches),('complete sort body differs',hex(old_rva))
        patches=[]
        for (off,kind,dependency),(off2,kind2,destination) in zip(old_patches,new_patches):
            assert (off,kind)==(off2,kind2)
            assert dependency in INDEX or dependency in EXCEPTIONS,('unexpected dependency',hex(dependency))
            if dependency in locations:assert locations[dependency]==destination,'dependency identity differs'
            else:locations[dependency]=destination
            if dependency in INDEX:
                pending.append(dependency);symbol=INDEX[dependency]
            else:symbol=len(BODIES)
            patches.append((off,kind,symbol))
        raw=bytes(target.mem[rva:rva+size])
        records[index]=(rva,size,fnv1a(raw),patches)
    assert all(r is not None for r in records),'incomplete native closure'
    for old,located in locations.items():
        assert any(s.VirtualAddress<=located<s.VirtualAddress+s.Misc_VirtualSize
                   and bool(s.Characteristics&0x20000000)==(old not in (0x87C778,0x87C738,0x87C748))
                   for s in target.pe.sections),'dependency section role'
    print('NATIVE_SORT',len(records),'bodies',sum(r[1] for r in records),'bytes',flush=True)
    return records


def refusals(reference,target,records):
    # Comparator addressing mode, sort entry opcode, record-field store,
    # and introsort argument setup. Refuse each before native execution.
    controls=((8,0x0B),(30,0),(4,0x15),(30,0x29))
    refused=0
    for index,off in controls:
        rva=records[index][0];old=target.mem[rva+off];target.mem[rva+off]=old^1
        try:
            with contextlib.redirect_stdout(io.StringIO()):verify(reference,target)
        except (AssertionError,ValueError,IndexError):refused+=1
        finally:target.mem[rva+off]=old
    assert refused==len(controls)
    print('Four altered sort contracts refused',flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('image',type=Path);p.add_argument('executable',type=Path)
    p.add_argument('--samples',type=int,default=21);p.add_argument('--self-test',action='store_true')
    a=p.parse_args();assert 0<=a.samples<=101
    reference,target=Image(BASELINE),Image(a.image);records=verify(reference,target)
    if a.self_test:refusals(reference,target,records)
    print('image_sha256',hashlib.sha256(a.image.read_bytes()).hexdigest(),flush=True)
    print('exe_sha256',hashlib.sha256(a.executable.read_bytes()).hexdigest(),flush=True)
    with tempfile.TemporaryDirectory(prefix='sage-native-sort-') as folder:
        path=Path(folder)/'manifest.txt';lines=[str(len(records))]
        for rva,size,digest,patches in records:
            lines.append(f'{rva} {size} {digest} {len(patches)}')
            lines.extend(' '.join(map(str,p)) for p in patches)
        path.write_bytes(('\n'.join(lines)+'\n').encode())
        command=[str(a.executable.resolve()),str(a.image.resolve()),str(path)]
        result=subprocess.call(command+[str(a.samples),'0'])
        if result:return result
        if a.self_test:
            bad=subprocess.run(command+['0','1'],capture_output=True,text=True)
            assert bad.returncode==1 and 'sort screen refused' in bad.stderr,bad
            print('Oracle rejects corrupted record assignment',flush=True)
    return 0


if __name__=='__main__':raise SystemExit(main())
