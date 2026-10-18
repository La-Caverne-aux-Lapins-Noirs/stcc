#!/usr/bin/env python3
"""A real parent PRG launches argument-checking children through GEMDOS Pexec.
No injection into the command tail or child environment by the debugger.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
from pathlib import Path
import arguments

ROOT=arguments.ROOT
OUT=ROOT/'tests/out/arguments-hatari'
PARENT='''
long execute_child(const char *path,const void *tail,const char *environment) {
long result;
__asm__ {
 move.l environment,-(sp)
 move.l tail,-(sp)
 move.l path,-(sp)
 clr.w -(sp)
 move.w #$4b,-(sp)
 trap #1
 lea 16(sp),sp
 move.l d0,result
}
return result;
}
void report(void) {
char *message="STCC PARENT RETURNED\\r\\n";
__asm__ {
 move.l message,-(sp)
 move.w #9,-(sp)
 trap #1
 addq.l #6,sp
}
}
int main(void) {
long result=execute_child("CHILD.TTP",tail,environment);
report();
return result==42L ? 42:1;
}
'''


def run(name, expected, tail, environment, length, hatari, tos):
    directory=OUT/name
    directory.mkdir(parents=True,exist_ok=True)
    arguments.OUT=directory
    arguments.compile_program('CHILD',arguments.probe(expected,tail,length))
    raw_tail=bytes([length])+tail+b'\0'
    declaration='unsigned char tail[]={'+','.join(map(str,raw_tail))+'};\n'
    declaration+='char environment[]={'+','.join(map(str,environment))+'};\n'
    parent=arguments.compile_program('parent',declaration+PARENT)
    shutil.copyfile(parent,directory/'TEST.TOS')
    (directory/'exit.bin').unlink(missing_ok=True)
    (directory/'start.ini').write_text(
        f'b GemdosOpcode = $4c :2 :trace :once :file {directory}/stop.ini\n')
    (directory/'stop.ini').write_text(f"savebin {directory}/exit.bin 'a7+2' 2\nquit\n")
    args=[hatari,'--tos',str(tos),'--machine','st','--cpulevel','0',
          '--cpuclock','8','--monitor','mono','--sound','off','--memsize','1',
          '--fast-forward','on','--confirm-quit','off','--conout','2',
          '--run-vbls','1800','--parse',str(directory/'start.ini'),str(directory/'TEST.TOS')]
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    result=subprocess.run(args,env=env,text=True,capture_output=True,timeout=40)
    log=result.stdout+result.stderr
    (directory/'hatari.log').write_text(log)
    assert result.returncode==0 and 'STCC PARENT RETURNED' in log,(name,log[-3500:])
    assert (directory/'exit.bin').is_file(),(name,log[-3500:])
    assert struct.unpack('>H',(directory/'exit.bin').read_bytes())[0]==42,(name,log[-3500:])
    print('PASS Hatari Pexec arguments',name)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--hatari',default='hatari')
    parser.add_argument('--tos',required=True,type=Path)
    options=parser.parse_args()
    args=['C:\\CLI\\CHILD.TTP']+['argument'+str(i) for i in range(70)]
    cases=[
        ('empty',[''],b'',b'X=1\0\0',0),
        ('quoted',['','alpha','two words','','C:\\TMP\\A.TXT'],
         b'alpha "two words" "" C:\\TMP\\A.TXT',b'X=1\0\0',32),
        ('extended',['C:\\CLI\\CHILD.TTP','a b','z'*300],b'ignored',
         b'X=1\0ARGV=value\0C:\\CLI\\CHILD.TTP\0a b\0'+b'z'*300+b'\0\0',127),
        ('allocated-vector',args,b'ignored',
         b'ARGV=\0'+b'\0'.join(a.encode() for a in args)+b'\0\0',127),
    ]
    # Compute the ordinary command length from the exact bytes used.
    cases[1]=(*cases[1][:4],len(cases[1][2]))
    for name,expected,tail,environment,length in cases:
        run(name,expected,tail,environment,length,options.hatari,options.tos.resolve())
    (OUT/'report.json').write_text(json.dumps({'tos':options.tos.name,
        'passed':[case[0] for case in cases],'child_launch':'GEMDOS Pexec'},indent=2)+'\n')
    print(len(cases),'parent/child argument runs passed')


if __name__=='__main__':main()
