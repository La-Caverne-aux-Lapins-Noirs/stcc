#!/usr/bin/env python3
"""Independent 68000 execution of register storage, IRQs and stack choices.
ROM tests need no external OS. --tos additionally runs PRGs on supplied TOS.
"""
import argparse
import json
import struct
from pathlib import Path
import rom_hatari
from rom import ROOT, compile_rom

OUT = ROOT/'tests/out/registers-hatari'
LARGE = '''
int main(void) { volatile unsigned char buffer[20000];
buffer[0]=19; buffer[19999]=23; return buffer[0]+buffer[19999]; }
'''
IRQ = '''
volatile unsigned int hits;
int helper(register(d3) int value) { value+=7; return value-6; }
__interrupt__ void handler(void) {
register(d3) int original=41;
register(a2) volatile unsigned int *counter=&hits;
if(helper(original)!=42 || original!=41) { hits=99; return; }
(*counter)++;
}
int main(void) {
register(d3) unsigned long preserved=0x12345678UL;
register(a2) volatile unsigned long *vector=(volatile unsigned long *)0x8cUL;
*vector=(unsigned long)handler;
__asm__ { trap #3 }
if(preserved!=0x12345678UL || vector!=(volatile unsigned long *)0x8cUL || hits!=1)
return 1;
return 42;
}
'''


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--hatari',default='hatari')
    parser.add_argument('--tos',type=Path)
    a=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    rom_hatari.OUT=OUT
    stack=OUT/'stack.s'
    stack.write_text('''        section bss
        xdef stack_end,_tail_guard
        ds.b 32768
stack_end:
_tail_guard:
        ds.l 1
''')
    guarded=LARGE.replace('int main', 'extern volatile unsigned long tail_guard;\nint main')
    guarded=guarded.replace('buffer[0]=19;', 'tail_guard=0x12345678UL; buffer[0]=19;')
    cases=[
        ('registers',(ROOT/'examples/16_registers.c').read_text(),[],[]),
        ('temporary-stack',(ROOT/'examples/17_stack.c').read_text(),[],[]),
        ('large-stack',LARGE,['--stack-size','32768'],[]),
        ('provided-stack',guarded,['--stack-top','stack_end'],[stack]),
        ('fixed-rom-stack',LARGE,['--stack-top','0x70000'],[]),
        ('interrupt',IRQ,[],[]),
    ]
    roms=[];prgs=[]
    for name,source,options,extra in cases:
        directory=OUT/name;directory.mkdir(exist_ok=True)
        for marker in ('halt.bin','fault.bin','rom.bin'):
            (directory/marker).unlink(missing_ok=True)
        path=directory/'main.c';path.write_text(source)
        image=directory/'test.rom';linkmap=directory/'test.map'
        compile_rom('--rom',path,*extra,*options,'-o',image,'--map',linkmap)
        ram,syms,_=rom_hatari.execute(name,image,linkmap,a.hatari)
        if name=='provided-stack':
            assert struct.unpack_from('>I',ram,syms['_tail_guard'])[0]==0x12345678
            assert syms['__stcc_program_end']>syms['__stcc_stack_end']
        if name=='interrupt':
            assert struct.unpack_from('>H',ram,syms['_hits'])[0]==1
        roms.append(name);print('PASS Hatari register/stack ROM',name)
        if a.tos and name not in ('fixed-rom-stack','interrupt'):
            from hatari import execute as execute_prg
            program=directory/'test.TOS'
            compile_rom(path,*extra,*options,'-o',program)
            execute_prg(program,42,a.hatari,a.tos.resolve(),'registers-'+name)
            prgs.append(name);print('PASS Hatari register/stack PRG',name)
    (OUT/'report.json').write_text(json.dumps({'rom':roms,'prg':prgs,
        'external_tos':a.tos.name if a.tos else None},indent=2)+'\n')
    print(len(roms),'ROM and',len(prgs),'PRG Hatari executions passed')


if __name__=='__main__':main()
