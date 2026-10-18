"""Verify complete native FX wrappers and pending-state application.

Read-only local-image research. Manifests contain only relocation metadata
and hashes; native code is loaded into private RAM by the offline fixture.
"""
import contextlib
import hashlib
import io
import struct
from pathlib import Path
import capstone
from capstone import x86
from test_fx_lifecycle import BASELINE, FUNCTIONS, verify as fx_verify, fnv1a
from verify_sites import Image
from port_sites import port

STATE_RVA, STATE_SIZE = 0x11D930, 746
DEPENDENCIES = (0x137590, 0x132AF8, 0x13D570, 0x13D180)
# Semantic regions; fixture symbol indices 16..26. These are research
# reference identities, never addresses installed into a game process.
DATA = (
    ('changed',0x9EC4F4,4),('skip_fixed',0x9EDA20,1),
    ('state',0x9EE5D8,0x290),('caps',0x9EDA7C,4),
    ('device',0x9EDA34,4),('light_enables',0x9ED5F0,4),
    ('api_count',0x9EDA98,4),('matrix_count',0x9EDA4C,4),
    ('vertex_count',0x9EDA54,4),('index_count',0x9EDA58,4),
    ('light_count',0x9EDA5C,4),
)

def recipe(image,rva):
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
    patches=[]; starts=set(); jumps=[]; total=0
    for ins in md.disasm(bytes(image.mem[rva:rva+STATE_SIZE]),image.base+rva):
        off=ins.address-image.base-rva; starts.add(off);total+=ins.size
        if ins.group(capstone.CS_GRP_JUMP):
            assert ins.operands[0].type==x86.X86_OP_IMM
            jumps.append(ins.operands[0].imm-image.base-rva)
        if ins.group(capstone.CS_GRP_CALL) and ins.operands[0].type==x86.X86_OP_IMM:
            dest=ins.operands[0].imm-image.base
            assert ins.bytes[0]==0xE8 and ins.size==5 and dest in DEPENDENCIES
            patches.append((off+1,0,12+DEPENDENCIES.index(dest),0))
        fields=[]
        if ins.disp_size==4 and image.base<=(ins.disp&0xffffffff)<image.base+len(image.mem):
            fields.append(off+ins.disp_offset)
        if not (ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP)) and ins.imm_size==4:
            if any(op.type==x86.X86_OP_IMM and image.base<=op.imm<image.base+len(image.mem) for op in ins.operands):
                fields.append(off+ins.imm_offset)
        for field in fields:
            dest=struct.unpack_from('<I',image.mem,rva+field)[0]-image.base
            matches=[(16+i,dest-lo) for i,(_,lo,size) in enumerate(DATA) if lo<=dest<lo+size]
            assert len(matches)==1,('unknown state global',hex(dest))
            patches.append((field,1,*matches[0]))
    assert total==STATE_SIZE and all(j in starts for j in jumps)
    return patches

def verify(reference,target):
    fx=fx_verify(reference,target)
    records=[(r,s,h,[(off,0,symbol,0) for off,symbol in calls]) for r,s,h,calls in fx]
    found=[]
    for rva in (STATE_RVA,)+DEPENDENCIES:
        _,hits=port(reference,rva,target,lengths=(STATE_SIZE,) if rva==STATE_RVA else (24,32,48,64,96,128))
        assert len(hits)==1,('state dependency not unique',hex(rva))
        found.append(hits[0])
    patches=recipe(reference,STATE_RVA)
    old=bytearray(reference.mem[STATE_RVA:STATE_RVA+STATE_SIZE])
    new=bytearray(target.mem[found[0]:found[0]+STATE_SIZE])
    symbols=[None]*27;symbols[8]=found[0];symbols[12:16]=found[1:]
    for off,kind,symbol,add in patches:
        value=struct.unpack_from('<I' if kind else '<i',new,off)[0]
        dest=value-target.base if kind else found[0]+off+4+value
        if symbols[symbol] is None: symbols[symbol]=dest-add
        assert dest==symbols[symbol]+add,'inconsistent state role'
        old[off:off+4]=new[off:off+4]=b'\0'*4
    assert old==new,'full state body differs'
    regions=[]
    for i,(_,_,size) in enumerate(DATA):
        lo=symbols[16+i];assert lo is not None
        assert any(s.VirtualAddress<=lo and lo+size<=s.VirtualAddress+s.Misc_VirtualSize and
                   s.Characteristics&0x80000000 and not s.Characteristics&0x20000000 for s in target.pe.sections)
        assert all(lo+size<=a or b<=lo for a,b in regions),'aliased state regions'
        regions.append((lo,lo+size))
    raw=bytes(target.mem[found[0]:found[0]+STATE_SIZE])
    records.append((found[0],STATE_SIZE,fnv1a(raw),patches))
    # Native Begin/Dynamic and the mesh's Draw must resolve the same body.
    for index in (1,3):
        for off,symbol in fx[index][3]:
            if symbol==8:
                actual=fx[index][0]+off+4+struct.unpack_from('<i',target.mem,fx[index][0]+off)[0]
                assert actual==found[0]
    print('apply_state:',STATE_SIZE,'bytes SHA256',hashlib.sha256(raw).hexdigest(),flush=True)
    return records

def refusals(reference,target):
    records=verify(reference,target);rva=records[8][0];count=0
    # Change a dirty bit, stride/layout access, API slot, final mask, and
    # a direct dependency. Every full-body/control contract must refuse it.
    for off in (0x1D,0x1D5,0x219,0x2D6,0x27):
        before=target.mem[rva+off];target.mem[rva+off]=before^1
        try:
            with contextlib.redirect_stdout(io.StringIO()): verify(reference,target)
        except (AssertionError,ValueError):count+=1
        finally:target.mem[rva+off]=before
    assert count==5
    print('Native state: five altered-body/dependency controls rejected',flush=True)

def manifest(path,records):
    lines=[]
    for rva,size,digest,patches in records:
        lines.append(f'{rva} {size} {digest} {len(patches)}')
        lines.extend(' '.join(map(str,p)) for p in patches)
    path.write_bytes(('\n'.join(lines)+'\n').encode())

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('image',type=Path)
    p.add_argument('--self-test',action='store_true');a=p.parse_args()
    reference,target=Image(BASELINE),Image(a.image)
    if a.self_test:refusals(reference,target)
    else:verify(reference,target)
