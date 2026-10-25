#!/usr/bin/env python3
"""Generate targeted instruction tests from integer mathematics, not the emulator.

Run from any directory; --check verifies committed fixtures without writing.
LLVM assembles compressed encodings independently. Expected
values use Python unbounded integers and explicit truncation/sign interpretation.
No production interpreter code or decoder is imported or called.

Each little-endian record contains 17 u32 words:
  length, instruction, initial_pc, ram_base, rs1, rs1_value, rs2, rs2_value,
  destination (32 means none), expected_result, expected_pc, stop, fault_address,
  memory_word_offset (512 means none), initial_memory_word, final_memory_word,
  retired_count.
Stop codes are fixture data: budget=0, breakpoint=1, ecall=2, undefined=3,
misaligned=4, unmapped=5; the harness explicitly maps them to enum values.
Registers not overridden have distinct sentinel values; RAM has a byte pattern.
A word of data and the instruction are installed before the state snapshot.
"""
import os
import sys
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'examples/riscv'
TEST = ROOT / 'sh/sysl/benchtop/riscv/instruction_tests.sysl'
MASK = (1 << 32) - 1
VALUES = [0, 1, 2, 0x7fffffff, 0x80000000, 0x80000001, 0xfffffffe, MASK, 0xaaaaaaaa, 0x55555555]
IMMS = [-2048, -1024, -1, 0, 1, 31, 32, 1024, 2047]
SHIFTS = [0, 1, 2, 4, 8, 15, 16, 30, 31, 32, 33, 63, MASK]
LAYOUTS = [(3,1,2), (1,1,2), (2,1,2), (3,1,1), (0,1,2), (3,0,2), (3,1,0), (31,30,31), (31,31,31)]
DEFAULT_PC = 256
RAM_SIZE = 512
BUDGET, BREAKPOINT, ECALL, UNDEFINED, MISALIGNED, UNMAPPED = range(6)
groups = {}
notes = {}
compressed = []

def u(x): return x & MASK
def signed(x): return x if x < (1 << 31) else x - (1 << 32)
def regs(): return [0] + [0x11220000 + i * 0x010101 for i in range(1,32)]

def case(name, code, *, length=4, pc=DEFAULT_PC, base=0, r1=1, a=0, r2=2, b=0,
         rd=32, result=0, next_pc=None, stop=BUDGET, at=0, mem_at=RAM_SIZE,
         memory=0xa5c37e81, after=None):
    next_pc = u(pc + length) if next_pc is None else u(next_pc)
    if stop != BUDGET: next_pc = pc
    result = 0 if rd == 0 else u(result)
    record = [length, code, pc, base, r1, u(a), r2, u(b), rd, result,
              next_pc, stop, u(at), mem_at, u(memory), u(memory if after is None else after), int(stop == BUDGET)]
    groups.setdefault(name, []).append(record)
    return record

def inputs(a,b,layout):
    rd,rs1,rs2=layout
    r=regs()
    if rs1:r[rs1]=u(a)
    if rs2:r[rs2]=u(b)
    return rd,rs1,rs2,r[rs1],r[rs2]

def ri(opcode,rd,op,rs1,imm):return ((imm & 4095)<<20)|(rs1<<15)|(op<<12)|(rd<<7)|opcode
def rr(rd,op,rs1,rs2,top=0):return (top<<25)|(rs2<<20)|(rs1<<15)|(op<<12)|(rd<<7)|0x33
def store(op,rs1,rs2,imm):return ((imm&0xfe0)<<20)|(rs2<<20)|(rs1<<15)|(op<<12)|((imm&31)<<7)|0x23
def branch(op,rs1,rs2,imm):
    n=imm&8191
    return ((n>>12)<<31)|(((n>>5)&63)<<25)|(rs2<<20)|(rs1<<15)|(op<<12)|(((n>>1)&15)<<8)|(((n>>11)&1)<<7)|0x63
def jal(rd,imm):
    n=imm&0x1fffff
    return ((n>>20)<<31)|(((n>>1)&1023)<<21)|(((n>>11)&1)<<20)|(((n>>12)&255)<<12)|(rd<<7)|0x6f

def quotient(a,b):
    # Python // rounds down; the architectural signed quotient truncates toward zero.
    if b == 0:return -1
    return (abs(a)//abs(b)) * (-1 if (a<0) != (b<0) else 1)

ALU = {
 'ADD':(0,0,lambda a,b:a+b), 'SUB':(0,32,lambda a,b:a-b),
 'SLL':(1,0,lambda a,b:a * (2 ** (b%32))),
 'SLT':(2,0,lambda a,b:int(signed(a)<signed(b))), 'SLTU':(3,0,lambda a,b:int(a<b)),
 'XOR':(4,0,lambda a,b:a^b), 'SRL':(5,0,lambda a,b:a//(2**(b%32))),
 'SRA':(5,32,lambda a,b:signed(a)//(2**(b%32))),
 'OR':(6,0,lambda a,b:a|b), 'AND':(7,0,lambda a,b:a&b),
}
M = {
 'MUL':lambda a,b:a*b,
 'MULH':lambda a,b:(signed(a)*signed(b))//(2**32),
 'MULHSU':lambda a,b:(signed(a)*b)//(2**32),
 'MULHU':lambda a,b:(a*b)//(2**32),
 'DIV':lambda a,b:quotient(signed(a),signed(b)),
 'DIVU':lambda a,b:MASK if b==0 else a//b,
 'REM':lambda a,b:signed(a) if b==0 else signed(a)-quotient(signed(a),signed(b))*signed(b),
 'REMU':lambda a,b:a if b==0 else a%b,
}

def arithmetic():
    for name,(op,top,fn) in ALU.items():
        operands=[(a,b,LAYOUTS[0]) for a in VALUES for b in (SHIFTS if name in ['SLL','SRL','SRA'] else VALUES)]
        operands += [(a,b,l) for l in LAYOUTS[1:] for a,b in [(0,0),(MASK,1),(0x80000000,MASK),(1,31),(0xaaaaaaaa,0x55555555)]]
        for a,b,l in operands:
            rd,rs1,rs2,x,y=inputs(a,b,l)
            case(name,rr(rd,op,rs1,rs2,top),r1=rs1,a=a,r2=rs2,b=b,rd=rd,result=fn(x,y))
        notes[name]='zero, signed extrema, carry/borrow wrap, alternating bits; source/destination aliases, x0, x31' + ('; shift counts 0/1/15/16/30/31/32/33/63/UINT_MAX' if name in ['SLL','SRL','SRA'] else '')
    for op,(name,fn) in enumerate(M.items()):
        pairs=[(a,b,LAYOUTS[0]) for a in VALUES+[3,u(-3),7,u(-7)] for b in VALUES+[3,u(-3),7,u(-7)]]
        pairs += [(a,b,l) for l in LAYOUTS[1:] for a,b in [(0,0),(MASK,1),(0x80000000,MASK),(u(-7),3),(7,u(-3))]]
        for a,b,l in pairs:
            rd,rs1,rs2,x,y=inputs(a,b,l)
            case(name,rr(rd,op,rs1,rs2,1),r1=rs1,a=a,r2=rs2,b=b,rd=rd,result=fn(x,y))
        notes[name]='zero/one, signed/unsigned extrema, all operand signs, high-word carries; aliases and x0; rejected without M'
        if name.startswith(('DIV','REM')):notes[name]+='; zero divisor (including 0/0), INT_MIN/-1, exact/inexact quotients'
    for name,(op,_,fn) in ALU.items():
        if name=='SUB':continue
        immediate_name={'ADD':'ADDI','SLL':'SLLI','SLT':'SLTI','SLTU':'SLTIU','XOR':'XORI','SRL':'SRLI','SRA':'SRAI','OR':'ORI','AND':'ANDI'}[name]
        immediates=list(range(32)) if name in ['SLL','SRL','SRA'] else IMMS
        for a in VALUES:
            for n in immediates:
                for rd,rs1 in [(3,1),(1,1),(0,1),(31,31),(3,0)]:
                    x=0 if rs1==0 else a
                    encoded=n | (1024 if name=='SRA' else 0)
                    case(immediate_name,ri(0x13,rd,op,rs1,encoded),r1=rs1,a=a,r2=0,rd=rd,result=fn(x,u(n)))
        notes[immediate_name]='zero, signed extrema, wrap and alternating bits; rd=rs1, x0 source/destination, x31; '+('all 32 legal shift amounts' if name in ['SLL','SRL','SRA'] else 'immediate -2048/-1024/-1/0/1/31/32/1024/2047')
    for name,opcode in [('LUI',0x37),('AUIPC',0x17)]:
        for n in [0,1,0x7ffff,0x80000,0xfffff]:
            for base in [0,0xfffffe00]:
                pc=u(base+256)
                for rd in [0,1,31]:
                    case(name,(n<<12)|(rd<<7)|opcode,pc=pc,base=base,rd=rd,result=(n<<12)+(pc if name=='AUIPC' else 0))
        notes[name]='zero, positive/sign-bit/all-one upper immediate; x0/x1/x31; nonzero PC and top-of-address-space wrap'

def control():
    for op,name,predicate in [(0,'BEQ',lambda a,b:a==b),(1,'BNE',lambda a,b:a!=b),(4,'BLT',lambda a,b:signed(a)<signed(b)),(5,'BGE',lambda a,b:signed(a)>=signed(b)),(6,'BLTU',lambda a,b:a<b),(7,'BGEU',lambda a,b:a>=b)]:
        for a,b in [(0,0),(0,1),(1,0),(MASK,0),(0x80000000,0x7fffffff),(MASK,MASK),(1,2),(2,1)]:
            for n in sorted({-4096, -4, -2, 0, 2, 4, 4094} | {sign * (1 << bit) for sign in [-1, 1] for bit in range(1, 12)}):
                for rs1,rs2 in [(1,2),(1,1),(0,2),(1,0),(31,30)]:
                    _,_,_,x,y=inputs(a,b,(3,rs1,rs2))
                    target=u(DEFAULT_PC+n) if predicate(x,y) else DEFAULT_PC+4
                    # The runner checks four profiles: halfword targets are valid only with C.
                    case(name,branch(op,rs1,rs2,n),r1=rs1,a=a,r2=rs2,b=b,next_pc=target)
        notes[name]='taken/untaken/equal/signed-vs-unsigned boundaries; aliased and x0 operands; -4096..4094 and every offset bit in both signs, zero/self/backward/halfword targets; IALIGN gating'
    for name in ['JAL','JALR']:
        for base in [0,0xfffffe00]:
            pc=u(base+256)
            if name=='JAL':
                for rd in [0,1,31]:
                    for n in sorted({-1048576, 0, 1048574} | {sign * (1 << bit) for sign in [-1, 1] for bit in range(1, 20)}):
                        case(name,jal(rd,n),pc=pc,base=base,rd=rd,result=u(pc+4),next_pc=u(pc+n))
            else:
                for rd,rs1 in [(3,1),(1,1),(0,1),(31,31),(3,0)]:
                    for a in [0,1,2,3,4,0x7fffffff,0x80000000,MASK]:
                        for n in [-2048,-1,0,1,2047]:
                            target=u((0 if rs1==0 else a)+n)&~1
                            case(name,ri(0x67,rd,0,rs1,n),pc=pc,base=base,r1=rs1,a=a,r2=0,rd=rd,result=u(pc+4),next_pc=target)
        notes[name]='x0/x1/x31 links; nonzero PC and address wrap; '+('full J immediate extrema and every immediate bit in both signs' if name=='JAL' else 'rd=rs1, x0 source, signed immediate extrema, low-bit clearing')+'; target alignment and no link write on fault'

def memory_cases():
    for name,op,width,signed_load in [('LB',0,1,True),('LH',1,2,True),('LW',2,4,False),('LBU',4,1,False),('LHU',5,2,False)]:
        for value in [0,1,0x7f,0x80,0xff,0x7fff,0x8000,0xffff,0x7fffffff,0x80000000,MASK,0x80ff7e81]:
            for n in [-2048,-1,0,1,2047]:
                for lane in range(0,4,width):
                    for rd,rs1 in [(3,1),(1,1),(0,1),(31,31)]:
                        address=64+lane
                        v=(value>>(8*lane))&((1<<(8*width))-1)
                        result=v-(1<<(8*width)) if signed_load and v&(1<<(8*width-1)) else v
                        case(name,ri(0x03,rd,op,rs1,n),r1=rs1,a=u(address-n),r2=0,rd=rd,result=result,mem_at=64,memory=value)
        # RAM edges, below/above mapping, every low alignment residue, zero-register base.
        for base in [0,0xfffffe00]:
            for address in [base, u(base+512-width), u(base-4), u(base+512), u(base+65), u(base+66), u(base+67)]:
                for rd in [0,1,31]:
                    stop=MISALIGNED if address%width else UNMAPPED if not base<=address<=base+512-width else BUDGET
                    offset=address-base if stop==BUDGET else RAM_SIZE
                    word_at=offset&~3 if offset<RAM_SIZE else RAM_SIZE
                    value=0x80ff7e81
                    lane=offset%4 if offset<RAM_SIZE else 0
                    v=(value>>(lane*8))&((1<<(8*width))-1)
                    result=v-(1<<(8*width)) if signed_load and v&(1<<(8*width-1)) else v
                    case(name,ri(0x03,rd,op,1,0),pc=u(base+256),base=base,r1=1,a=address,r2=0,rd=rd,result=result,stop=stop,at=address,mem_at=word_at,memory=value)
        case(name,ri(0x03,3,op,0,64),r1=0,r2=0,rd=3,result=(0x81-256 if signed_load and width==1 else 0x7e81 if width==2 else 0x80ff7e81 if width==4 else 0x81),mem_at=64,memory=0x80ff7e81)
        notes[name]='width/sign boundaries, every valid byte lane, +/- immediate extrema, rd=base and x0/x31; first/last mapped access, every misalignment residue, below/above RAM, address wrap; faults preserve state'
    for name,op,width in [('SB',0,1),('SH',1,2),('SW',2,4)]:
        for value in VALUES:
            for n in [-2048,-1,0,1,2047]:
                for lane in range(0,4,width):
                    address=64+lane
                    mask=((1<<(8*width))-1)<<(8*lane)
                    after=(0xa5c37e81&~mask)|((value<<(8*lane))&mask)
                    case(name,store(op,1,2,n),a=u(address-n),b=value,mem_at=64,after=after)
        for base in [0,0xfffffe00]:
            for address in [base,u(base+512-width),u(base-4),u(base+512),u(base+65),u(base+66),u(base+67)]:
                for rs1,rs2 in [(1,2),(1,1),(0,2),(1,0),(31,30)]:
                    a=address;b=0x12345678
                    if rs1==rs2:b=a
                    actual=0 if rs1==0 else a
                    value=0 if rs2==0 else b
                    stop=MISALIGNED if actual%width else UNMAPPED if not base<=actual<=base+512-width else BUDGET
                    offset=actual-base if stop==BUDGET else RAM_SIZE
                    word_at=offset&~3 if offset<RAM_SIZE else RAM_SIZE
                    lane=offset%4 if offset<RAM_SIZE else 0
                    mask=((1<<(8*width))-1)<<(8*lane)
                    after=(0xa5c37e81&~mask)|((value<<(8*lane))&mask)
                    case(name,store(op,rs1,rs2,0),pc=u(base+256),base=base,r1=rs1,a=a,r2=rs2,b=b,stop=stop,at=actual,mem_at=word_at,after=after if stop==BUDGET else None)
        notes[name]='byte truncation and untouched neighbouring bytes; every valid lane, signed immediate extrema; base=data aliases, x0 base/data, x31; RAM edges, misalignment, unmapped faults with no partial store'

def system():
    for pred in range(16):
        for succ in range(16):
            for rd,rs1 in [(0,0),(1,2),(31,31)]:
                case('FENCE',ri(0x0f,rd,0,rs1,(pred<<4)|succ))
    for mode in range(1,16):
        for masks in [0,0x33,0x88,0xff]:
            for rd,rs1 in [(0,0),(1,2),(31,31)]:
                case('FENCE',ri(0x0f,rd,0,rs1,(mode<<8)|masks))
    notes['FENCE']='TSO/reserved-mode fallback, all 256 predecessor/successor combinations; ignored rd/rs1 fields; entire register/RAM state preserved; PC and retirement advance once'
    for name,code,stop in [('ECALL',0x73,ECALL),('EBREAK',0x100073,BREAKPOINT)]:
        for base in [0,0xfffffe00]:
            case(name,code,pc=u(base+256),base=base,stop=stop,at=u(base+256))
        notes[name]='exact stop reason and fault PC; no register/RAM writes or retirement; stopped CPUs do not resume'

# LLVM, rather than handwritten compressed encoders, supplies the instruction bits.
def cc(name,assembly,**kwargs):
    record=case(name,0,length=2,**kwargs)
    compressed.append((assembly,record))

def compressed_cases():
    for rd in [8,15]:
        for n in [4,8,16,32,64,128,256,512,1020]:
            for a in [0,0x7fffffff,0xfffffff0]:
                cc('C.ADDI4SPN',f'c.addi4spn x{rd}, sp, {n}',r1=2,a=a,r2=0,rd=rd,result=a+n)
    for name in ['C.ADDI','C.LI','C.ANDI']:
        for rd in ([8,15] if name=='C.ANDI' else [0,1,2,31]):
            for n in [-32,-16,-8,-4,-2,-1,0,1,2,4,8,15,16,31]:
                for a in VALUES:
                    result=a+n if name=='C.ADDI' else n if name=='C.LI' else a&u(n)
                    cc(name,f'{name.lower()} x{rd}, {n}',r1=rd,a=a,r2=0,rd=rd,result=result)
    for n in [-512,-256,-128,-64,-32,-16,16,32,64,128,256,496]:
        for a in [0,0x7fffffff,0xfffffff0]:
            cc('C.ADDI16SP',f'c.addi16sp sp, {n}',r1=2,a=a,r2=0,rd=2,result=a+n)
    for rd in [0,1,31]:
        for n in [1,2,4,8,16,31,0xfffff,0xfffe0]:
            cc('C.LUI',f'c.lui x{rd}, {n}',rd=rd,result=n<<12)
    for name in ['C.SLLI','C.SRLI','C.SRAI']:
        for rd in ([0,1,2,31] if name=='C.SLLI' else [8,15]):
            for n in range(32):
                for a in VALUES:
                    result=a*(2**n) if name=='C.SLLI' else a//(2**n) if name=='C.SRLI' else signed(a)//(2**n)
                    cc(name,f'{name.lower()} x{rd}, {n}',r1=rd,a=a,r2=0,rd=rd,result=result)
    for name in ['C.SUB','C.XOR','C.OR','C.AND','C.ADD','C.MV']:
        for rd,rs in ([(0,1),(1,1),(1,31),(31,1),(31,31)] if name in ['C.ADD','C.MV'] else [(8,8),(8,15),(15,8),(15,15)]):
            for a,b in [(0,0),(MASK,1),(0x80000000,MASK),(0xaaaaaaaa,0x55555555),(1,MASK)]:
                if rd==rs:b=a
                x=0 if rd==0 else a
                result={'C.SUB':lambda:x-b,'C.XOR':lambda:x^b,'C.OR':lambda:x|b,'C.AND':lambda:x&b,'C.ADD':lambda:x+b,'C.MV':lambda:b}[name]()
                cc(name,f'{name.lower()} x{rd}, x{rs}',r1=rd,a=a,r2=rs,b=b,rd=rd,result=result)
    for name in ['C.J','C.JAL']:
        for n in [-2048,-1024,-512,-256,-128,-64,-32,-16,-8,-4,-2,0,2,4,8,16,32,64,128,256,512,1024,2046]:
            for base in [0,0xfffffe00]:
                pc=u(base+256)
                cc(name,f'{name.lower()} .{n:+}',pc=pc,base=base,rd=1 if name=='C.JAL' else 32,result=pc+2,next_pc=pc+n)
    for name in ['C.JR','C.JALR']:
        for rs in [1,2,31]:
            for target in [0,1,2,3,4,0x7fffffff,0x80000000,MASK]:
                cc(name,f'{name.lower()} x{rs}',r1=rs,a=target,r2=0,rd=1 if name=='C.JALR' else 32,result=DEFAULT_PC+2,next_pc=target&~1)
    for name in ['C.BEQZ','C.BNEZ']:
        for rs in [8,15]:
            for a in [0,1,MASK,0x80000000]:
                for n in [-256,-128,-64,-32,-16,-8,-4,-2,0,2,4,8,16,32,64,128,254]:
                    taken=(a==0) if name=='C.BEQZ' else (a!=0)
                    cc(name,f'{name.lower()} x{rs}, .{n:+}',r1=rs,a=a,r2=0,next_pc=DEFAULT_PC+n if taken else DEFAULT_PC+2)
    for name in ['C.LW','C.SW','C.LWSP','C.SWSP']:
        load=name in ['C.LW','C.LWSP'];sp=name.endswith('SP')
        offsets=[0,4,8,16,32,64,128,252] if sp else [0,4,8,16,32,64,124]
        layouts=([(1,2),(2,2),(31,2)] if load else [(0,2),(1,2),(2,2),(31,2)]) if sp else [(8,8),(8,15),(15,8),(15,15)]
        for data,rs in layouts:
            for n in offsets:
                for address in [0,64,508,512,65,66,67,MASK]:
                    a=u(address-n)
                    value=0x80ff7e81
                    b=a if data==rs else value
                    actual=address
                    stop=MISALIGNED if actual%4 else UNMAPPED if actual>508 else BUDGET
                    after=0 if data==0 else b
                    cc(name,f'{name.lower()} x{data}, {n}(x{rs})',r1=rs,a=a,r2=0 if load else data,b=b,rd=data if load else 32,result=value,stop=stop,at=actual,mem_at=address if stop==BUDGET else RAM_SIZE,memory=value,after=value if load or stop!=BUDGET else after)
    cc('C.NOP','c.nop')
    cc('C.EBREAK','c.ebreak',stop=BREAKPOINT,at=DEFAULT_PC)
    for name in [n for n in groups if n.startswith('C.')]:
        notes[name]='LLVM-assembled execution, not just expansion; register/RAM preservation, PC/retirement, both C profiles; rejected without C'
    for name in ['C.ADDI4SPN','C.ADDI16SP','C.ADDI','C.LI','C.LUI','C.ANDI']:
        notes[name]+='; immediate extrema and every immediate bit, signed/wrap values, legal register limits and hints'
    for name in ['C.SLLI','C.SRLI','C.SRAI']:
        notes[name]+='; all 32 legal shift amounts, sign extension, x0 hints where legal'
    for name in ['C.SUB','C.XOR','C.OR','C.AND','C.ADD','C.MV']:
        notes[name]+='; aliases, low/high registers, zero/extrema/alternating bits, x0 hints where legal'
    for name in ['C.LW','C.SW','C.LWSP','C.SWSP']:
        notes[name]+='; every offset bit and extrema, base/data aliases, first/last mapped word, misalignment, unmapped, no partial writes'
    for name in ['C.J','C.JAL','C.BEQZ','C.BNEZ']:
        notes[name]+='; signed offset extrema and every offset bit, zero/backwards/halfword targets'+('; taken/untaken' if 'B' in name else '; link/address wrap')
    for name in ['C.JR','C.JALR']:
        notes[name]+='; source/link aliases, x31, low-bit clearing and halfword targets'


def assemble_compressed():
    clang=os.environ.get('CLANG','/opt/homebrew/opt/llvm/bin/clang')
    linker=os.environ.get('LD_LLD','/opt/homebrew/opt/lld/bin/ld.lld')
    objcopy=os.environ.get('OBJCOPY','/opt/homebrew/opt/llvm/bin/llvm-objcopy')
    with tempfile.TemporaryDirectory(prefix='riscv-instructions-') as temp:
        d=Path(temp)
        text='.section .text,"ax",@progbits\n.globl _start\n_start:\n.option rvc\n'+''.join('    '+asm+'\n' for asm,_ in compressed)
        (d/'cases.S').write_text(text)
        subprocess.run([clang,'--target=riscv32-unknown-elf','-march=rv32ic','-mabi=ilp32','-mno-relax','-c',str(d/'cases.S'),'-o',str(d/'cases.o')],check=True)
        subprocess.run([linker,'--no-relax','-T',str(OUT/'link.ld'),str(d/'cases.o'),'-o',str(d/'cases.elf')],check=True)
        subprocess.run([objcopy,'-O','binary',str(d/'cases.elf'),str(d/'cases.bin')],check=True)
        data=(d/'cases.bin').read_bytes()
        assert len(data)==2*len(compressed),(len(data),len(compressed))
        for i,(_,record) in enumerate(compressed):record[1]=struct.unpack_from('<H',data,2*i)[0]

EXPECTED_I='LUI AUIPC JAL JALR BEQ BNE BLT BGE BLTU BGEU LB LH LW LBU LHU SB SH SW ADDI SLTI SLTIU XORI ORI ANDI SLLI SRLI SRAI ADD SUB SLL SLT SLTU XOR SRL SRA OR AND FENCE ECALL EBREAK'.split()
EXPECTED_M='MUL MULH MULHSU MULHU DIV DIVU REM REMU'.split()
EXPECTED_C='C.ADDI4SPN C.LW C.SW C.NOP C.ADDI C.JAL C.LI C.ADDI16SP C.LUI C.SRLI C.SRAI C.ANDI C.SUB C.XOR C.OR C.AND C.J C.BEQZ C.BNEZ C.SLLI C.LWSP C.JR C.MV C.EBREAK C.JALR C.ADD C.SWSP'.split()

def emit():
    assert set(groups)==set(EXPECTED_I+EXPECTED_M+EXPECTED_C)
    data=bytearray()
    wrappers=['// Generated by examples/riscv/generate-instruction-tests.py. Keep the harness separate.\nmodule sh.sysl.benchtop.riscv\n@tests\n']
    matrix=['# RISC-V instruction coverage\n',
        'Generated by `python3 examples/riscv/generate-instruction-tests.py`. Every implemented mnemonic\n'
        'has its own named unit test. These are targeted architectural boundary tests, **not exhaustive\n'
        'enumeration of every operand/state combination**, hardware certification, or coverage of the\n'
        'unimplemented Hazard3 extensions.\n',
        'Expected results use Python arbitrary-precision arithmetic, explicit 32-bit truncation, and\n'
        'mathematical signed interpretation. Compressed encodings are produced independently by LLVM.\n'
        'The generator does not import the interpreter or call its decoder. The original 118-pair\n'
        'LLVM expansion corpus and compiled-C host oracle remain separate cross-checks.\n',
        'Every vector checks all 32 registers, every byte of the 512-byte RAM, PC, stop reason,\n'
        'fault address on stops, and retirement. All four core profiles are exercised, including\n'
        'rejection of M/C instructions where disabled. Taken halfword-aligned control transfers\n'
        'must fault without C, preserving the link and all other state. Faulting/stopped CPUs are\n'
        'run again to verify no further effects. Instruction fetch and reserved/hint encodings\n'
        'have additional structural tests in `instruction_boundaries_tests.sysl`.\n',
        '| Instruction | Vectors | Targeted boundaries and invariants |\n|---|---:|---|']
    first=0
    for name in EXPECTED_I+EXPECTED_M+EXPECTED_C:
        records=groups[name]
        for record in records:data.extend(struct.pack('<17I',*record))
        need=1 if name in EXPECTED_M else 4 if name.startswith('C.') else 0
        wrappers += [f'\n@test("RV32 instruction {name}: architectural boundaries and state preservation")\ninstruction_{name.lower().replace(".","_")}()\n    check_instruction_vectors("{name}", {first}, {len(records)}, {need})\n']
        matrix.append(f'| {name} | {len(records)} | {notes[name]} |')
        first+=len(records)
    matrix+=['',f'{len(groups)} instructions; {first:,} vectors, each checked on four profiles ({first*4:,} executions).',
        '', 'Rebuild the fixtures with Python 3 and LLVM (`CLANG`, `LD_LLD`, `OBJCOPY` overrides supported).',
        'Run `sysl test . --filter "RV32 instruction"` for the per-mnemonic tests and `sysl test .` for the full suite.',
        '', 'References:',
        '- [RV32I](https://docs.riscv.org/reference/isa/v20260120/unpriv/rv32.html)',
        '- [M extension](https://docs.riscv.org/reference/isa/v20260120/unpriv/m-st-ext.html)',
        '- [C extension](https://docs.riscv.org/reference/isa/v20260120/unpriv/c-st-ext.html)', '']
    products = {
        OUT/'instructions.bin': bytes(data),
        TEST: ''.join(wrappers).encode(),
        ROOT/'sh/sysl/benchtop/riscv/COVERAGE.md': '\n'.join(matrix).encode(),
    }
    for path, contents in products.items():
        if '--check' in sys.argv:
            if path.read_bytes() != contents: raise SystemExit(f'out of date: {path}')
        else: path.write_bytes(contents)
    print(f'{len(groups)} instructions, {first} vectors, {len(data)} bytes')

if __name__=='__main__':
    arithmetic();control();memory_cases();system();compressed_cases();assemble_compressed();emit()
