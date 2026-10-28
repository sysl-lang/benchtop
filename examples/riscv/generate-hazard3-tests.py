#!/usr/bin/env python3
"""Independent mathematical + LLVM oracle for Hazard3's additional scalar instructions.

Uses the existing fixture format/harness. Run with --check to verify committed artifacts.
Custom instructions use .insn because LLVM does not name Hazard3-specific instructions.
Stateful CSRs, traps, PMP, interrupts, sleep and Zcmp have separate named unit tests.
"""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'examples/riscv'
sys.dont_write_bytecode = True
spec=importlib.util.spec_from_file_location('base_vectors', OUT/'generate-instruction-tests.py')
v=importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)
assemblies=[]
requirements={}

def add(name,asm,need,**kw):
    record=v.case(name,0,**kw)
    assemblies.append((asm,record))
    requirements[name]=need

def revbits(x,width):return int(f'{x:0{width}b}'[::-1],2)
def zipbits(x):return sum(((x>>i)&1)<<(2*i) | ((x>>(16+i))&1)<<(2*i+1) for i in range(16))
def unzip(x):return sum(((x>>(2*i))&1)<<i | ((x>>(2*i+1))&1)<<(16+i) for i in range(16))
def rol(a,b):n=b%32;return v.u(a*(2**n)) | a//(2**(32-n)) if n else a
def ror(a,b):return rol(a,(-b)%32)
def sx(x,bits):x=x%2**bits;return x-2**bits if x>=2**(bits-1) else x

BREG={
 'SH1ADD':lambda a,b:a*2+b, 'SH2ADD':lambda a,b:a*4+b, 'SH3ADD':lambda a,b:a*8+b,
 'ANDN':lambda a,b:a&~b, 'ORN':lambda a,b:a|~b, 'XNOR':lambda a,b:~(a^b),
 'MIN':lambda a,b:a if v.signed(a)<v.signed(b) else b,
 'MAX':lambda a,b:a if v.signed(a)>v.signed(b) else b,
 'MINU':min,'MAXU':max,'ROL':rol,'ROR':ror,
 'BCLR':lambda a,b:a&~(2**(b%32)), 'BSET':lambda a,b:a|(2**(b%32)),
 'BINV':lambda a,b:a^(2**(b%32)), 'BEXT':lambda a,b:(a//(2**(b%32)))%2,
 'PACK':lambda a,b:(a%65536)|((b%65536)*65536), 'PACKH':lambda a,b:(a%256)|((b%256)*256),
}
BUNARY={
 'CLZ':lambda a:32-a.bit_length(), 'CTZ':lambda a:(a&-a).bit_length()-1 if a else 32,
 'CPOP':lambda a:bin(a).count('1'),'SEXT.B':lambda a:sx(a,8),'SEXT.H':lambda a:sx(a,16),
 'ZEXT.H':lambda a:a%65536,'ORC.B':lambda a:sum((255 if (a//(256**i))%256 else 0)*(256**i) for i in range(4)),
 'REV8':lambda a:int.from_bytes(a.to_bytes(4,'little'),'big'),
 'BREV8':lambda a:sum(revbits((a>>(8*i))&255,8)<<(8*i) for i in range(4)),
 'ZIP':zipbits,'UNZIP':unzip,
}

def generate():
    layouts=v.LAYOUTS
    for name,fn in BREG.items():
        for a in v.VALUES:
            for b in [0,1,2,4,8,15,16,31,32,33,63,0x80000000,0xffffffff]:
                for layout in layouts:
                    rd,rs1,rs2,x,y=v.inputs(a,b,layout)
                    add(name,f'{name.lower()} x{rd}, x{rs1}, x{rs2}',16,r1=rs1,a=a,r2=rs2,b=b,rd=rd,result=fn(x,y))
    for name,fn in BUNARY.items():
        for a in v.VALUES+[0x80,0x8000,0x7f,0x7fff,0xff,0xffff,0x01000000]:
            for rd,rs1 in [(3,1),(1,1),(0,1),(31,31),(3,0)]:
                add(name,f'{name.lower()} x{rd}, x{rs1}',16,r1=rs1,a=a,r2=0,rd=rd,result=fn(0 if rs1==0 else a))
    for name,fn in [('RORI',ror),('BCLRI',BREG['BCLR']),('BSETI',BREG['BSET']),('BINVI',BREG['BINV']),('BEXTI',BREG['BEXT'])]:
        for a in v.VALUES:
            for n in range(32):
                for rd,rs1 in [(3,1),(1,1),(0,1),(31,31),(3,0)]:
                    add(name,f'{name.lower()} x{rd}, x{rs1}, {n}',16,r1=rs1,a=a,r2=0,rd=rd,result=fn(0 if rs1==0 else a,n))
    for name in ['H3.BEXTM','H3.BEXTMI']:
        for size in range(1,9):
            for a in v.VALUES:
                for n in (v.SHIFTS if name=='H3.BEXTM' else range(32)):
                    for layout in layouts[:7]:
                        rd,rs1,rs2,x,y=v.inputs(a,n,layout) if name=='H3.BEXTM' else (layout[0],layout[1],0,0 if layout[1]==0 else a,n)
                        if name=='H3.BEXTM':asm=f'.insn r 0x0b, 0, {(size-1)*2}, x{rd}, x{rs1}, x{rs2}'
                        else:asm=f'.insn i 0x0b, 4, x{rd}, x{rs1}, {(size-1)*64+n}'
                        add(name,asm,128,r1=rs1,a=a,r2=rs2,b=n,rd=rd,result=(x//(2**(y%32)))%(2**size))
    for op,name in [(0,'AMOADD.W'),(1,'AMOSWAP.W'),(4,'AMOXOR.W'),(8,'AMOOR.W'),(12,'AMOAND.W'),(16,'AMOMIN.W'),(20,'AMOMAX.W'),(24,'AMOMINU.W'),(28,'AMOMAXU.W'),(2,'LR.W'),(3,'SC.W')]:
        for old in v.VALUES:
            for value in v.VALUES:
                for rd,rs1,rs2 in [(3,1,2),(1,1,2),(2,1,2),(0,1,2),(31,30,31)]:
                    for order in ['', '.aq', '.rl', '.aqrl']:
                        actual=old
                        if op==2:
                            asm=f'lr.w{order} x{rd}, (x{rs1})';rs2=0;value=0
                        elif op==3:asm=f'sc.w{order} x{rd}, x{rs2}, (x{rs1})'
                        else:
                            asm=f'{name.lower()}{order} x{rd}, x{rs2}, (x{rs1})'
                            actual={0:lambda:old+value,1:lambda:value,4:lambda:old^value,8:lambda:old|value,12:lambda:old&value,16:lambda:old if v.signed(old)<v.signed(value) else value,20:lambda:old if v.signed(old)>v.signed(value) else value,24:lambda:min(old,value),28:lambda:max(old,value)}[op]()
                        add(name,asm,2,r1=rs1,a=64,r2=rs2,b=value,rd=rd,result=1 if op==3 else old,mem_at=64,memory=old,after=actual)
    add('FENCE.I','fence.i',64)
    for name,fn in [('C.ZEXT.B',lambda a:a%256),('C.SEXT.B',lambda a:sx(a,8)),('C.ZEXT.H',lambda a:a%65536),('C.SEXT.H',lambda a:sx(a,16)),('C.NOT',lambda a:~a)]:
        for reg in [8,15]:
            for a in v.VALUES+[0x80,0xff,0x7f,0x8000,0x7fff,0xffff]:
                add(name,f'{name.lower()} x{reg}',32,length=2,r1=reg,a=a,r2=0,rd=reg,result=fn(a))
    for a in v.VALUES:
        for b in v.VALUES:
            for rd,rs2 in [(8,8),(8,15),(15,8),(15,15)]:
                rhs=a if rd==rs2 else b
                add('C.MUL',f'c.mul x{rd}, x{rs2}',32,length=2,r1=rd,a=a,r2=rs2,b=rhs,rd=rd,result=a*rhs)
    for name,load,width,signed_load in [('C.LBU',True,1,False),('C.LHU',True,2,False),('C.LH',True,2,True),('C.SB',False,1,False),('C.SH',False,2,False)]:
        for offset in (range(4) if width==1 else [0,2]):
            for value in v.VALUES+[0x80ff7e81]:
                for data,base in [(8,8),(8,15),(15,8),(15,15)]:
                    addr=64+offset
                    a=64
                    raw=(value>>(offset*8))%(2**(width*8))
                    result=sx(raw,width*8) if signed_load else raw
                    if data==base:value=a
                    initial=0x80ff7e81 if not load else value
                    if load:
                        raw=(initial>>(offset*8))%(2**(width*8))
                        result=sx(raw,width*8) if signed_load else raw
                    mask=((1<<(width*8))-1)<<(offset*8)
                    after=initial if load else (initial&~mask)|((value<<(offset*8))&mask)
                    add(name,f'{name.lower()} x{data}, {offset}(x{base})',32,length=2,r1=base,a=a,r2=0 if load else data,b=value,rd=data if load else 32,result=result,mem_at=64,memory=initial,after=after)


def assemble():
    with tempfile.TemporaryDirectory(prefix='hazard3-vectors-') as temp:
        d=Path(temp)
        text='.section .text,"ax",@progbits\n.globl _start\n_start:\n'
        for asm,record in assemblies:
            text+=('.option arch, +zca,+zcb,+zcmp\n' if record[0]==2 else '.option norvc\n')+asm+'\n'
        (d/'oracle.S').write_text(text)
        subprocess.run([os.environ.get('CLANG','/opt/homebrew/opt/llvm/bin/clang'),'--target=riscv32-unknown-elf','-march=rv32ima_zicsr_zifencei_zba_zbb_zbs_zbkb_zca_zcb_zcmp','-mabi=ilp32','-mno-relax','-c',str(d/'oracle.S'),'-o',str(d/'oracle.o')],check=True)
        subprocess.run([os.environ.get('LD_LLD','/opt/homebrew/opt/lld/bin/ld.lld'),'--no-relax','-T',str(OUT/'link.ld'),str(d/'oracle.o'),'-o',str(d/'oracle.elf')],check=True)
        subprocess.run([os.environ.get('OBJCOPY','/opt/homebrew/opt/llvm/bin/llvm-objcopy'),'-O','binary',str(d/'oracle.elf'),str(d/'oracle.bin')],check=True)
        data=(d/'oracle.bin').read_bytes();at=0
        assert len(data)==sum(r[0] for _,r in assemblies)
        for _,record in assemblies:
            record[1]=int.from_bytes(data[at:at+record[0]],'little');at+=record[0]


def emit():
    out=bytearray();first=0
    source='// Generated by examples/riscv/generate-hazard3-tests.py.\nmodule sh.sysl.benchtop.riscv\n@tests\n\nprivate val HAZARD3_VECTORS = embed("../../../../testdata/riscv/hazard3-instructions.bin")\n'
    matrix='# Hazard3 additional instruction vectors\n\nIndependent Python mathematics and LLVM-assembled instructions; every vector checks the\nwhole register/RAM state across all five profiles. Standard instructions are shared with\nexisting profiles, not separate Hazard3 implementations. The stateful extension tests\n(CSR/traps/PMP/IRQs/power/Zcmp restart) live in `hazard3_tests.sysl`.\n\n| Instruction | Vectors |\n|---|---:|\n'
    for name,records in v.groups.items():
        for record in records:out.extend(struct.pack('<17I',*record))
        source+=f'\n@test("Hazard3 instruction {name}: architectural boundaries and extension gating")\nhazard3_{name.lower().replace(".","_")}()\n    check_vectors(HAZARD3_VECTORS, "{name}", {first}, {len(records)}, {requirements[name]})\n'
        matrix+=f'| {name} | {len(records)} |\n';first+=len(records)
    matrix+='\nStateful instruction tests in `hazard3_tests.sysl`:\n\n'
    matrix+='| Instruction | Targeted coverage |\n|---|---|\n'
    for name in ['CSRRW','CSRRS','CSRRC','CSRRWI','CSRRSI','CSRRCI']:
        matrix+=f'| {name} | All source register/immediate fields, x0 suppression, aliases, CSR state changes; privilege/read-only/unknown CSR rules |\n'
    for name,coverage in [
        ('MRET','Machine/User return, MIE/MPIE/MPP/MPRV and IRQ context stack'),
        ('WFI','Sleep retirement, enabled-pending wake with global MIE clear, TW'),
        ('H3.BLOCK','Paired wake, latched events, consume once, TW, MSLEEP depth'),
        ('H3.UNBLOCK','Peer/sender signalling, pre-arriving events and TW'),
        ('CM.PUSH','All 12 register lists and 4 stack adjustments; exact saved layout, restartable faults'),
        ('CM.POP','All lists/adjustments, register restore, SP commit and restartable faults'),
        ('CM.POPRET','All lists/adjustments, restore and return'),
        ('CM.POPRETZ','All lists/adjustments, restore/return/a0 commit; partial-transfer faults'),
        ('CM.MVSA01','All 64 register pairs; reject identical destinations'),
        ('CM.MVA01S','All 64 register pairs; allow identical sources'),
    ]:matrix+=f'| {name} | {coverage} |\n'
    matrix+='\nAdditional state tests cover all 52 IRQ inputs, all priority windows and unimplemented\nwindows, priority ties/nesting, force clearing/read suppression, CLEARTS, PMP granularity/\nlocks/first-match/partial-fetch rules and read/execute permissions (E6), MPRV, M-mode region\nenforcement, User-mode interrupt masking (E7), counters, LR/SC/AMO faults and paired-hart\nreservation invalidation/unblock signals. These are targeted architectural boundaries, not\nexhaustive state enumeration or physical-board differential validation.\n'
    matrix+=f'\n{len(v.groups)} additional vector-tested instructions plus 16 named stateful instructions, {first:,} vectors; five profiles.\n\nRegenerate with `python3 examples/riscv/generate-hazard3-tests.py` or verify with `--check`.\n'
    products={ROOT/'testdata/riscv/hazard3-instructions.bin':bytes(out),ROOT/'sh/sysl/benchtop/riscv/hazard3_instruction_tests.sysl':source.encode(),ROOT/'sh/sysl/benchtop/riscv/HAZARD3-COVERAGE.md':matrix.encode()}
    for path,data in products.items():
        if '--check' in sys.argv:
            if path.read_bytes()!=data:raise SystemExit(f'out of date: {path}')
        else:path.write_bytes(data)
    print(f'{len(v.groups)} instructions, {first} vectors, {len(out)} bytes')

if __name__=='__main__':generate();assemble();emit()
