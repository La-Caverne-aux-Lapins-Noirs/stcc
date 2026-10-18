#!/usr/bin/env python3
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
EXAMPLES=[('01MIN.TOS',['01_minimal.c'],42),('02CALC.TOS',['02_calculs.c'],0),
 ('03STRUCT.TOS',['03_structures.c'],0),('04RECUR.TOS',['04_recursion.c'],0),
 ('05GEMDOS.TOS',['05_gemdos.c'],0),('06KEY.TOS',['06_clavier.c'],65),
 ('07FILE.TOS',['07_fichier.c'],0),('08VIDEO.PRG',['08_video.c'],0),
 ('09MULTI.TOS',['09_multi/main.c','09_multi/math.c'],0),
 ('10MIX.TOS',['10_mix/main.c','10_mix/twice.s'],0),
 ('11BITS.TOS',['11_bits.c'],0),('12IRQ.TOS',['12_interrupts.c'],0),
 ('13CHAIN.TOS',['13_naked_chain.c'],0),('14VBL.TOS',['14_vbl.c'],0),
 ('16REG.TOS',['16_registers.c'],42),('17STACK.TOS',['17_stack.c'],42),
 ('18ARGS.TTP',['18_args.c'],0),('19PORT.TOS',['19_portable.c'],42)]
OS_ONLY={'12IRQ.TOS','13CHAIN.TOS','14VBL.TOS'}

def main():
 out=ROOT/'examples/bin';out.mkdir(exist_ok=True)
 try:from m68k import run
 except ImportError:run=None
 for name,files,expected in EXAMPLES:
  args=[ROOT/'stcc',*[ROOT/'examples'/f for f in files],'-o',out/name]
  subprocess.run([str(x) for x in args],check=True)
  if run and name not in OS_ONLY:
   state=run(out/name,limit=12000000)
   assert state['exit']==expected,(name,state['exit'],expected)
   if name=='07FILE.TOS':assert state['files']['STCC.TXT'].startswith(b'Ecrit par')
  print('PASS example',name,'compile + execute' if run and name not in OS_ONLY else 'compile (OS execution in tests/hatari.py)')

if __name__=='__main__':main()
