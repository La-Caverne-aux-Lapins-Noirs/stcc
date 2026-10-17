#!/usr/bin/env python3
"""Check that raw entry code has no hidden instructions and IRQ exits share RTE.
Actual CPU exception entry, nesting and context restoration: examples 12/13
executed by tests/hatari.py, not the TRAP doubles in tests/m68k.py.
"""
import json, os, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    out = ROOT / 'tests/out'
    out.mkdir(exist_ok=True)
    source = out / 'function_modes.c'
    assembly = out / 'function_modes.s'
    source.write_text('''volatile int hits;
__naked__ void raw(void){__asm__ {
 nop
 rts
}}
__interrupt__ void irq(void){int x=hits;if(x)return;hits++;}
__interrupt__ __naked__ void raw_irq(void){__asm__ {
 rte
}}
int main(void){return 0;}
''')
    subprocess.run([os.environ.get('STCC', str(ROOT/'stcc')), '-S', str(source),
                    '-o', str(assembly)], check=True)
    text = assembly.read_text()
    raw = text.split('_raw:\n',1)[1].split('        xdef _irq',1)[0]
    instructions = lambda part: [line.strip() for line in part.splitlines()
                                if line.strip() and not line.strip().endswith(':')]
    assert instructions(raw) == ['nop','rts'], raw
    irq = text.split('_irq:\n',1)[1].split('        xdef _raw_irq',1)[0]
    ins = instructions(irq)
    assert ins[:2] == ['movem.l d0-d7/a0-a6,-(sp)', 'link a6,#-2'], irq
    assert ins[-3:] == ['unlk a6','movem.l (sp)+,d0-d7/a0-a6','rte'], irq
    assert ins.count('rte') == 1 and 'rts' not in ins, irq
    assert any(line.startswith('bra.w __stcc_gen_') for line in ins), irq
    raw_irq = text.split('_raw_irq:\n',1)[1].split('        xdef _main',1)[0]
    assert instructions(raw_irq) == ['rte'], raw_irq
    report = {'raw_without_hidden_instructions':True,
              'irq_saves_restores_full_context':True,
              'early_return_uses_common_RTE':True,
              'combined_interrupt_naked_is_raw':True}
    (out/'function-modes-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS function entry modes: raw bytes contract, full IRQ save and shared RTE')

if __name__ == '__main__':
    main()
