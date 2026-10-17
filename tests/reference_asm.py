#!/usr/bin/env python3
"""Optional encoding oracle. GNU tools are used ONLY by this test, never stcc.
Run with: python3 tests/reference_asm.py --as m68k-linux-gnu-as
The reference reads independently written test assembly, not compiler sources.
"""
import argparse,json,re,struct,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]


def elf_text(path):
    b=path.read_bytes();assert b[:6]==b'\x7fELF\x01\x02'
    start,=struct.unpack_from('>I',b,32)
    size,num,names=struct.unpack_from('>HHH',b,46)
    sh=[struct.unpack_from('>10I',b,start+i*size) for i in range(num)]
    table=b[sh[names][4]:sh[names][4]+sh[names][5]]
    for s in sh:
        name=table[s[0]:].split(b'\0',1)[0]
        if name==b'.text':return b[s[4]:s[4]+s[5]]
    raise AssertionError('reference object has no .text')


def vectors():
    result=[]
    def add(op):result.append(op)
    srcs=['d0','d7','a0','a7','(a0)','(a7)+','-(a3)','16(a2)',
          '-8(a6,d1.w)','0(a1,a2.l)','$1234.w','$123456.l','#17']
    dsts=['d1','a3','(a2)','(sp)+','-(a2)','12(a4)','4(a3,d2.l)','$1200.w','$123400.l']
    for size in ['b','w','l']:
        for src in srcs:
            for dst in dsts:
                if size=='b' and (src in ['a0','a7'] or dst=='a3'):continue
                add(f'move.{size} '+(src if size!='l' or src!='#17' else '#$12345678')+f',{dst}')
        for op in ['add','sub','cmp','and','or']:
            for src in srcs:
                if size=='b' and src in ['a0','a7']:continue
                if op in ['and','or'] and src in ['a0','a7']:continue
                add(f'{op}.{size} {src},d2')
        for op in ['add','sub','and','or','eor']:
            for dst in dsts:
                if dst=='a3':continue
                if op in ['add','sub','and','or'] and dst=='d1':continue
                add(f'{op}.{size} d0,{dst}')
        for op in ['addi','subi','cmpi','andi','ori','eori']:
            for dst in dsts:
                if dst!='a3':add(f'{op}.{size} #17,{dst}')
        for op in ['addq','subq']:
            for dst in dsts:
                if size=='b' and dst=='a3':continue
                add(f'{op}.{size} #7,{dst}')
        for op in ['neg','negx','not','clr','tst']:
            for dst in dsts:
                if dst!='a3':add(f'{op}.{size} {dst}')
        for op in ['asl','asr','lsl','lsr','rol','ror','roxl','roxr']:
            for count in ['#1','#8','d3']:add(f'{op}.{size} {count},d2')
        for op in ['addx','subx']:
            add(f'{op}.{size} d0,d1');add(f'{op}.{size} -(a0),-(a1)')
        add(f'cmpm.{size} (a0)+,(a1)+')
    for size in ['w','l']:
        for op in ['adda','suba','cmpa']:
            for src in srcs:add(f'{op}.{size} {src},a2')
        for regs in ['d0','d2-d7/a2-a5','d0-d7/a0-a6']:
            for addr in ['(a0)','16(a2)','$123456.l','-(sp)']:
                add(f'movem.{size} {regs},{addr}')
            for addr in ['(a0)','16(a2)','$123456.l','(sp)+']:
                add(f'movem.{size} {addr},{regs}')
        for op in ['movep']:
            add(f'{op}.{size} d3,12(a2)');add(f'{op}.{size} 12(a2),d3')
        add(f'ext.{size} d3')
    for op in ['mulu','muls','divu','divs','chk']:
        for src in srcs:
            if src not in ['a0','a7']:add(f'{op}.w {src},d2')
    for op in ['btst','bchg','bclr','bset']:
        for bit in ['#5','d1']:
            for dst in ['d3','(a0)','12(a1)','$123456.l']:add(f'{op} {bit},{dst}')
    for op in ['lea','pea','jmp','jsr']:
        for src in ['(a0)','16(a2)','4(a3,d2.l)','$1234.w','$123456.l']:
            add(f'{op} {src}'+(',a3' if op=='lea' else ''))
    for op in ['abcd','sbcd']:
        add(f'{op} d0,d1');add(f'{op} -(a0),-(a1)')
    for op in ['tas','nbcd']:
        for dst in ['d0','(a0)','4(a2)']:add(f'{op} {dst}')
    for cc in ['t','f','hi','ls','cc','cs','ne','eq','vc','vs','pl','mi','ge','lt','gt','le']:
        for dst in ['d0','(a0)','12(a3)']:add(f's{cc} {dst}')
    for op in ['asl','asr','lsl','lsr','rol','ror','roxl','roxr']:add(f'{op}.w (a0)')
    result+=['exg d0,d7','exg a0,a7','exg d0,a7','swap d3','link a6,#-20',
      'unlk a6','move.l usp,a0','move.l a0,usp','move.w sr,d0',
      'move.w d0,sr','move.w d0,ccr','ori.b #17,ccr','andi.w #17,sr',
      'eori.w #17,sr','nop','rts','rte','rtr','reset','trapv','illegal','stop #$2700']
    for i in range(16):add(f'trap #{i}')
    return result


def main():
    p=argparse.ArgumentParser();p.add_argument('--as',dest='assembler',default='m68k-linux-gnu-as');a=p.parse_args()
    passed=[]
    with tempfile.TemporaryDirectory(prefix='stcc-reference-') as td:
        d=Path(td)
        for line in vectors():
            own=d/'own.s';own.write_text('        '+line+'\n')
            subprocess.run([str(ROOT/'stcc'),'--nostartfiles',str(own),'-o',str(d/'own.TOS')],check=True,capture_output=True)
            gas=re.sub(r'\$([0-9a-fA-F]+)',r'0x\1',line)
            # Motorola source and GNU differ on case/USP syntax only by naming.
            (d/'ref.s').write_text('        '+gas+'\n')
            r=subprocess.run([a.assembler,'-m68000','--register-prefix-optional',str(d/'ref.s'),'-o',str(d/'ref.o')],capture_output=True,text=True)
            assert r.returncode==0,(line,r.stderr)
            reference=elf_text(d/'ref.o')
            tos=(d/'own.TOS').read_bytes();n,=struct.unpack_from('>I',tos,2)
            ours=tos[28:28+n]
            assert ours==reference,(line,ours.hex(),reference.hex())
            passed.append(line)
    out=ROOT/'tests/out';out.mkdir(exist_ok=True)
    (out/'asm-reference.json').write_text(json.dumps({'passed':len(passed),'instructions':passed},indent=2)+'\n')
    print(f'PASS {len(passed)} instruction encodings match independent GNU m68k-as')

if __name__=='__main__':main()
