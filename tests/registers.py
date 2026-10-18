#!/usr/bin/env python3
"""Execute register storage, call preservation and configurable PRG stacks.
Unicorn is optional; byte/diagnostic checks always run. Hatari adds IRQ checks.
"""
import json
import os
import re
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'tests/out/registers'


def compile_case(name, source, *options, extras=(), expected=42):
    path = OUT/(name+'.c')
    path.write_text(source)
    compiler = str(os.environ.get('STCC', ROOT/'stcc'))
    assembly = path.with_suffix('.s')
    subprocess.run([compiler, '-S', str(path), '-o', str(assembly)], check=True)
    image = path.with_suffix('.TOS')
    subprocess.run([compiler, str(path), *map(str, extras), *map(str, options),
                    '-o', str(image), '--map', str(path.with_suffix('.map'))], check=True)
    if execute:
        for base in (0x20000, 0x34000):
            state = execute(image, basepage=base, limit=12000000)
            assert state['exit'] == expected, (name, state['exit'], expected)
    return image, assembly.read_text()


try:
    from m68k import run as execute
except ImportError:
    execute = None


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    passed = []

    def case(name, source, *options, **kwargs):
        result = compile_case(name, source, *options, **kwargs)
        passed.append(name)
        print('PASS registers', name, '(execute x2)' if execute else '(compile only)')
        return result

    _, asm = case('automatic', '''
int main(void) { register int i=0; register int total=0;
while(i<7) { total+=i; i++; } return total*2; }
''')
    assert 'link a6,#-0' in asm and 'movem.l d2/d3,-(sp)' in asm

    case('typed', '''
int main(void) {
register(d2) unsigned char byte=255U; register(d3) signed char negative=-7;
register(d4) unsigned int word=65535U; register(d5) long value=-123456L;
byte++; negative--; word++; value+=123498L;
if(byte!=0 || negative!=-8 || word!=0) return 1;
return (int)value;
}
''')
    case('compound', '''
int main(void) { register(d6) unsigned long x=100UL;
x+=25UL; x*=4UL; x/=5UL; x%=73UL; x<<=2; x>>=1;
x|=1UL; x^=3UL; x&=63UL; x-=10UL;
return (int)x; }
''')  # 100 ->125 ->500 ->100 ->27 ->108 ->54 ->55 ->52 ->52 ->42
    case('postfix', '''
int main(void) { register(d3) int x=39; int a=x++; int b=++x;
if(a!=39 || b!=41 || x--!=41 || --x!=39) return 1; return x+3; }
''')
    case('pointer', '''
int main(void) { int a[4]={12,13,17,99}; register(a4) int *p=a;
register int result=*p++; result+=*p; p+=1; result+=*p;
if(p-a!=2) return 1; return result; }
''')
    case('register-parameters', '''
int f(register(a3) int *p, register(d6) signed char x) {
__asm__ { addq.b #1,x }
return *p+x; }
int main(void) { int a=50; int (*call)(int *,signed char)=f;
return call(&a,-9); }
''')
    case('recursive', '''
unsigned long fac(register(d4) int n) { register(d5) unsigned long saved=(unsigned long)n;
if(n<2) return 1UL; return saved*fac(n-1); }
int main(void) { return fac(5)==120UL ? 42:1; }
''')
    names = ['d2','d3','d4','d5','d6','d7','a2','a3','a4','a5']
    declarations = lambda offset: '\n'.join(
        f'register({r}) unsigned long v{i}={offset+i}UL;' for i,r in enumerate(names))
    checks = ' || '.join(f'v{i}!={100+i}UL' for i in range(10))
    case('all-registers-across-call', 'int other(void) {'+declarations(200)+
         ' return (int)(v0+v9); }\nint main(void) {'+declarations(100)+
         ' int result=other(); if(result!=409 || '+checks+') return 1; return 42; }\n')
    case('scope-reuse', '''
int main(void) { int result=0;
{ register(d3) int x=19; result+=x; }
{ register(d3) int x=23; result+=x; } return result; }
''')
    case('future-explicit-reservation', '''
int main(void) { register int first=19; register(d2) int second=23;
return first+second; }
''')
    _, asm = case('pressure-fallback', '''
int main(void) { register int a=1,b=2,c=3,d=4,e=5,f=6,g=7;
return a+b+c+d+e+f+g+14; }
''')
    assert 'link a6,#-2' in asm
    case('inline-automatic-preserved', '''
int main(void) { register int x=42;
__asm__ { moveq #3,d2 } return x; }
''')
    case('inline-automatic-operand', '''
int main(void) { register int x=41;
__asm__ { addq.w #1,x } return x; }
''')
    case('inline-explicit-raw-register', '''
int main(void) { register(d3) unsigned long x=41UL;
__asm__ { addq.l #1,d3 } return (int)x; }
''')
    case('macro-binding', '''
#define CHOSEN d4
int main(void) { register(CHOSEN) int answer=42; return answer; }
''')
    for name in ('16_registers', '17_stack'):
        case('example-'+name, (ROOT/'examples'/(name+'.c')).read_text())

    minimal = 'int main(void) { return 42; }\n'
    default, _ = case('default-stack', minimal)
    enlarged, _ = case('sized-stack', minimal, '--stack-size', '32768')
    bss = lambda image: struct.unpack_from('>I', image.read_bytes(), 10)[0]
    assert bss(enlarged)-bss(default) == 16384
    case('large-local-with-sized-stack', '''
int main(void) { volatile unsigned char buffer[20000];
buffer[0]=19; buffer[19999]=23; return buffer[0]+buffer[19999]; }
''', '--stack-size', '32768')
    stack = OUT/'provided-stack.s'
    stack.write_text('''        section bss
        xdef provided_stack_end,_tail_guard
        ds.b 32768
provided_stack_end:
_tail_guard:
        ds.l 1
''')
    image, _ = case('provided-stack-keeps-tail', '''
extern volatile unsigned long tail_guard;
int main(void) { volatile unsigned char buffer[20000];
tail_guard=0x12345678UL; buffer[0]=19; buffer[19999]=23;
if(tail_guard!=0x12345678UL) return 1; return buffer[0]+buffer[19999]; }
''', '--stack-top', 'provided_stack_end', extras=(stack,))
    mapping = image.with_suffix('.map').read_text()
    syms = {m[2]:int(m[1],16) for m in re.finditer(r'^([0-9a-f]{8}) [GL] (\S+) ', mapping,re.M)}
    assert syms['__stcc_stack_end'] == syms['provided_stack_end']
    assert syms['__stcc_program_end'] > syms['provided_stack_end']

    bad = [
        ('scratch-register', 'int main(void){register(d0) int x;return 0;}', 'd2-d7'),
        ('frame-register', 'int main(void){register(a6) long x;return 0;}', 'reserved'),
        ('stack-register', 'int main(void){register(sp) long x;return 0;}', 'reserved'),
        ('invalid-register', 'int main(void){register(d8) int x;return 0;}', 'd2-d7'),
        ('conflict', 'int main(void){register(d3) int a,b;return 0;}', 'live variable'),
        ('nested-conflict', 'int main(void){register(d3) int a;{register(d3) int b;}return 0;}',
         'live variable'),
        ('small-address-register', 'int main(void){register(a2) int x;return 0;}', '32-bit'),
        ('aggregate-register', 'int main(void){register(d3) int x[2];return 0;}', 'integer or pointer'),
        ('global-register', 'register(d3) int x;int main(void){return 0;}', 'file-scope'),
        ('address-explicit', 'int main(void){register(d3) int x;return &x!=0;}', 'address of register'),
        ('address-fallback', 'int main(void){register int a,b,c,d,e,f,g;return &g!=0;}',
         'address of register'),
        ('naked-register', '__naked__ void f(void){register(d3) int x;}int main(void){return 0;}',
         '__naked__ bodies'),
    ]
    compiler = str(os.environ.get('STCC', ROOT/'stcc'))
    for name, source, diagnostic in bad:
        path=OUT/(name+'.c'); path.write_text(source)
        r=subprocess.run([compiler,str(path),'-o',str(OUT/'invalid.TOS')],capture_output=True,text=True)
        assert r.returncode and diagnostic in r.stderr,(name,r.stderr)
        passed.append(name);print('PASS registers reject',name)
    path=OUT/'options.c';path.write_text(minimal)
    for options, diagnostic in [
        (['--stack-size','0'],'stack size'), (['--stack-size','257'],'stack size'),
        (['--stack-size','32768','--stack-top','end'],'mutually exclusive'),
        (['--nostartfiles','--stack-size','32768'],'default startup'),
        (['--stack-top','0x70000'],'startup stack top'),
        (['--stack-top','_main'],'startup stack top'),
        (['--rom','--stack-top','0x78002'],'startup stack top'),
        (['--rom','--stack-size','524288'],'RAM overflow'),
    ]:
        r=subprocess.run([compiler,str(path),*options,'-o',str(OUT/'invalid.TOS')],
                         capture_output=True,text=True)
        assert r.returncode and diagnostic in r.stderr,(options,r.stderr)
        passed.append('reject '+' '.join(options));print('PASS stack reject',*options)
    (OUT/'report.json').write_text(json.dumps({'passed':len(passed),'cpu_execution':bool(execute),
                                            'tests':passed},indent=2)+'\n')
    if not execute:print('SKIP register/stack CPU execution: Unicorn unavailable')
    print(len(passed),'register and stack checks passed')


if __name__=='__main__':main()
