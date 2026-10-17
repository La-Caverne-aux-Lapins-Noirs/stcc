#!/usr/bin/env python3
"""Compiler regression tests. The execution core is optional; skips are explicit."""
import json
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'tests/out'
OUT.mkdir(exist_ok=True)
CC=Path(os.environ.get('STCC',str(ROOT/'stcc')))
passed=[]
skipped=[]

def command(args, good=True):
    r=subprocess.run([str(x) for x in args],text=True,capture_output=True)
    if good and r.returncode:
        raise AssertionError(f'{args}\n{r.stdout}{r.stderr}')
    if not good:
        assert r.returncode, f'invalid input accepted: {args}'
        assert 'error:' in r.stderr, r.stderr
    return r

def build(name,source,other=None):
    path=OUT/(name+'.c');path.write_text(source)
    exe=OUT/(name+'.TOS')
    inputs=[path]
    for ext,text in (other or {}).items():
        p=OUT/(name+'_'+ext);p.write_text(text);inputs.append(p)
    command([CC,*inputs,'-o',exe,'--map',OUT/(name+'.map')])
    return exe

try:
    from m68k import run,header
except ImportError:
    run=None
    def header(path):
        b=path.read_bytes();h=struct.unpack('>H6IH',b[:28])
        assert h[0]==0x601a and h[-1]==0
        return b,*h[1:5]
    skipped.append('Unicorn 68000 execution: python3-unicorn unavailable')

CASES={
'minimal':('int main(void){return 42;}',42),
'local':('int main(void){int a=17,b=4; a+=b*3; return a-5;}',24),
'int16':('int main(void){unsigned int x=65535U; int y=-7; return (x+2U==1U) && (y/3==-2) && (y%3==-1) && (sizeof(int)==2) && (sizeof(long)==4);}',1),
'promotions':('int main(void){unsigned char c=255; unsigned short s=65535U; long l=-2L; return (c+1==256) && (s+1U==0U) && (l+s==65533L) && (-1<1U)==0;}',1),
'casts':('int main(void){unsigned long a=4294967295UL; char c=(char)255; unsigned char d=(unsigned char)-1; return c==-1 && d==255 && (int)a==-1 && ((unsigned long)-1)==a;}',1),
'longarith':('int main(void){unsigned long a=4000000000UL,b=1234567UL; return a/b==3240UL && a%b==2920UL && (a*17UL)==3575490560UL && (-2000000000L/17L)==-117647058L;}',1),
'shifts':('int main(void){unsigned int a=32768U; int b=-16; return (a>>3)==4096U && (b>>2)==-4 && (1U<<15)==32768U && ((0x55^0xaa)&255)==255;}',1),
'loops':('int main(void){int i=0,s=0;for(i=0;i<10;i++){if(i==2)continue;if(i==8)break;s+=i;}do{s--; }while(s>20);while(i>0)i--;return s+i;}',20),
'switch':('int f(int x){int r=0;switch(x){case -1:r=3;break;case 0:case 1:r=5;break;default:r=7;}return r;}int main(void){return f(-1)+f(0)+f(9);}',15),
'goto':('int main(void){int i=0;again:i++;if(i<7)goto again;return i;}',7),
'recursion':('int fib(int x){if(x<2)return x;return fib(x-1)+fib(x-2);}int main(void){return fib(9);}',34),
'array':('int main(void){int a[5]={1,2,3};int *p=a; p+=2; *p=8;return a[0]+*(a+1)+a[2]+a[3]+a[4]+(p-a);}',13),
'multidim':('int main(void){int a[2][3]={{1,2,3},{4,5,6}};int (*p)[3]=a;return p[1][2]+sizeof(a)+sizeof(*p);}',24),
'record':('struct S{char c;long l;short a[2];};int main(void){struct S x={3,100L,{4,5}};struct S y;struct S *p=&x;y=x;y.a[1]++;return p->c+(int)y.l+y.a[1]+sizeof(x);}',119),
'union_enum':('enum E{A=-2,B,C=7};union U{unsigned long l;unsigned char c[4];};int main(void){union U u={0x12345678UL};return u.c[0]+u.c[3]+B+C+sizeof(union U);}',148),
'function_pointer':('typedef int (*Op)(int,int);int add(int a,int b){return a+b;}int sub(int a,int b){return a-b;}int apply(Op f,int a,int b){return f(a,b);}int main(void){Op a[2]={add,&sub};return apply(a[0],7,3)+(*a[1])(7,3);}',14),
'static':('int g;static int x=3;int f(void){static int n=2;n++;g+=n;return n;}int main(void){int a=f();return a+f()+g+x;}',17),
'string':('char a[]="abc";const char *p="xyz";int main(void){char b[5]="hi";return sizeof(a)+sizeof(b)+a[1]+p[2]+b[1]+b[3];}',334),
'pointer_init':('int a[4]={1,2,3,4};int *p=&a[2];int (*f)(void);int get(void){return *p;}int main(void){f=get;return f()+((char*)p-(char*)a);}',7),
'shortcircuit':('int main(void){int i=0;int j=0&&++i;int k=1||++i;return (i==0)&&(j==0)&&(k==1)&&((i?17:9)==9);}',1),
'compound_once':('int main(void){int a[3]={2,4,6};int i=0;int x=a[i++]++;a[i++] *= 3;return x+a[0]+a[1]+i;}',19),
'comma':('int main(void){int a=1,b=2;return (a+=b,b+=a,a+b);}',8),
'params':('long f(char a,unsigned char b,short c,long d,int *p){return a+b+c+d+*p;}int main(void){int x=7;return (int)f(-1,255,-3,1000L,&x);}',1258),
'inline_local':('int f(int x){int y=5;__asm__ (d0,d1,a0,a1,cc,memory) {\n move.w x,d0\n add.w y,d0\n move.w d0,y\n move.l #$12345678,d2\n}\nreturn y;}int main(void){return f(37);}',42),
'inline_trap':('void print(const char *message){__asm__ {\n move.l message,-(sp)\n move.w #9,-(sp)\n trap #1\n addq.l #6,sp\n}}int main(void){print("STCC OK\\r\\n");return 0;}',0),
'inline_labels':('int main(void){int x=0;__asm__ {\n moveq #3,d0\n.loop:\n addq.w #1,x\n dbra d0,.loop\n}\n__asm__ {\n moveq #1,d0\n.loop:\n addq.w #1,x\n dbra d0,.loop\n}\nreturn x;}',6),
'preprocessor':('#define CAT(a,b) a##b\n#define S(x) #x\n#define ADD(a,b) ((a)+(b))\n#define VALUE 17\n#if defined(VALUE) && VALUE > 10\nint CAT(gl,obal)=ADD(VALUE,3);\n#else\n#error bad\n#endif\nint main(void){const char *s=S(hello world);return global+s[5];}',52),
'nested_macro':('#define A 5\n#define B(x) ((x)+A)\n#define C(x) B(x)\nint main(void){return C(7);}',12),
'multifile':('extern int shared;int add(int,int);int main(void){return add(shared,3);}',12),
'asm_module':('int twice(int);int main(void){return twice(21);}',42),
}
CASES.update({
'pp_rescan':('#define F(x) ((x)+1)\n#define ALIAS F\nint main(void){return ALIAS(F(3));}',5),
'null_constant':('int main(void){int *p=1-1;return p==0;}',1),
'implicit_int':('sum(int a,int b){return a+b;}main(void){return sum(3,4);}',7),
'basepage':('#include <stcc.h>\n#include <stddef.h>\nint main(void){return sizeof(struct stcc_basepage)==256 && offsetof(struct stcc_basepage,command_line)==128 && stcc_basepage->text_length>0;}',1),

'asm_pc':('int get(void);int main(void){return get();}',42),
'asm_macro_module':('int get(void);int main(void){return get();}',31),
'asm_saved':('int probe(void);void worker(void){__asm__(d2,d3,a2){\n moveq #0,d2\n moveq #0,d3\n suba.l a2,a2\n}}int main(void){return probe();}',1),

'const_pointer':('int main(void){int n=7;int *const p=&n;const int *q=p;return *q;}',7),
'aggregate_init':('struct S{int x;};int main(void){struct S a={7};struct S b=a;const struct S *p=&b;return p->x;}',7),
'stdarg':('#include <stdarg.h>\nint sum(int n,...){va_list ap;int s=0;va_start(ap,n);while(n--)s+=va_arg(ap,int);va_end(ap);return s;}int main(void){return sum(4,3,7,8,9);}',27),
'stddef':('#include <stddef.h>\nstruct S{char a;long b;};int main(void){return sizeof(size_t)+offsetof(struct S,b);}',6),
'pp_integer':('#if (-1 < 1U) || (0 && (1/0)) || ((1?7:9)!=7)\n#error bad condition\n#endif\nint main(void){return __LINE__;}\n',4),
'pp_multiline':('#define ADD(a,b) ((a)+(b))\nint main(void){return ADD(3,\n7);}',10),
'asm_macro':('int main(void){int x=0;__asm__ {\n rept 3\n addq.w #1,x\n endr\n}return x;}',3),
})

CASES.update({
'bits_layout':(r"""struct S {unsigned int a:3; signed int b:5; unsigned int :2; unsigned int c:6;};
struct S s={5,-3,42};
int main(void){unsigned char *p=(unsigned char*)&s;return sizeof(s)==2 && p[0]==0xbd && p[1]==0x2a && s.a==5 && s.b==-3 && s.c==42;}""",1),
'bits_units':(r"""struct S{char x;unsigned int a:12;unsigned int b:8;unsigned int :0;unsigned int c:1;char y;};
struct S s={9,0xabc,0xde,1,7};
int main(void){unsigned char *p=(unsigned char*)&s;return sizeof(s)==10 && p[0]==9 && p[2]==0xab && p[3]==0xc0 && p[4]==0xde && p[5]==0 && p[6]==0x80 && p[8]==7;}""",1),
'bits_promote':(r"""struct S{unsigned int a:3;int b:3;unsigned int c:16;};
int main(void){struct S s={7,-3,65535U};return s.a+1==8 && -1<s.a && s.b<0 && s.b/2==-1 && s.c+1==0U && (-1<s.c)==0 && sizeof(+s.a)==2;}""",1),
'bits_updates':(r"""struct S{unsigned int a:3;int b:5;unsigned int c:8;};struct S s={6,-4,0xa5};int calls;
struct S *get(void){calls++;return &s;}
int main(void){int old=get()->a++;int now=++get()->a;get()->b-=2;get()->b*=2;get()->c^=0xff;return calls==5 && old==6 && now==0 && s.a==0 && s.b==-12 && s.c==0x5a;}""",1),
'bits_initializers':(r"""struct S{unsigned int :4;unsigned int a:4;unsigned int :8;unsigned int b:3;unsigned int c:5;};
struct S g[2]={{3,5,9},{7}};
int main(void){struct S s={11};static struct S t={4,6,13};struct S x;x=t;return g[0].a==3 && g[0].b==5 && g[0].c==9 && g[1].a==7 && !g[1].b && !g[1].c && s.a==11 && !s.b && !s.c && x.a==4 && x.b==6 && x.c==13;}""",1),
'bits_union':(r"""union U{unsigned int :3;unsigned int a:4;unsigned long b;};union U u={10};
int main(void){union U v={5};unsigned char *p=(unsigned char*)&u;return sizeof(u)==4 && p[0]==0xa0 && !p[1] && !p[2] && !p[3] && v.a==5;}""",1),
'bits_volatile':(r"""struct S{unsigned int a:4;unsigned int b:4;unsigned int c:8;};volatile struct S v={1,2,3};
int main(void){int x=(v.b=7);v.a|=8;v.c>>=1;return x==7 && v.a==9 && v.b==7 && v.c==1;}""",1),
})
# Every legal width exercises sign extension, narrowing and preservation of a neighbour.
_fields=[];_checks=[]
for _w in range(1,17):
    _fields.append(f'struct B{_w}{{unsigned int u:{_w};int :0;int s:{_w};unsigned int guard:1;}} b{_w};')
    _checks.append(f'b{_w}.u={(1<<_w)-1}U;b{_w}.s={-(1<<(_w-1))};b{_w}.guard=1;'
                   f'if(b{_w}.u!={(1<<_w)-1}U || b{_w}.s!={-(1<<(_w-1))} || b{_w}.guard!=1)return {_w};'
                   f'b{_w}.u+=1U;b{_w}.s=0;'
                   f'if(b{_w}.u || b{_w}.s || b{_w}.guard!=1)return {_w+16};')
CASES['bits_all_widths']=(''.join(_fields)+'int main(void){'+''.join(_checks)+'return 0;}',0)

CASES.update({
'naked_call':(r"""int hits;
__naked__ void raw(void){__asm__ {
 addq.w #1,hits
 rts
}}
int main(void){raw();raw();return hits;}""",2),
'irq_declarations':(r"""typedef __interrupt__ void (*Irq)(void);
__interrupt__ void tick(void);
__interrupt__ void tick(void){int n=3;while(n--){}return;}
Irq p=tick;
int main(void){return p==tick;}""",1),
})

NEGATIVE={
'undeclared':('int main(void){return nope;}','undeclared'),
'lvalue':('int main(void){3=4;return 0;}','lvalue'),
'const':('int main(void){const int a=1;a=2;return a;}','lvalue'),
'bad_return':('void f(void){return 3;}int main(void){return 0;}','void'),
'bad_call':('int f(int);int main(void){return f(1,2);}','arguments'),
'duplicate_case':('int main(void){switch(1){case 1:;case 1:;}return 0;}','duplicate'),
'bad_break':('int main(void){break;return 0;}','break'),
'bad_goto':('int main(void){goto missing;return 0;}','label'),
'const_pointer_write':('int main(void){int x;int *const p=&x;p=&x;return 0;}','lvalue'),
'deep_qualifier':('int main(void){int **p=0;const int **q=p;return 0;}','pointer'),
'conditional_qualifier':('int main(void){int x=0;const int y=1;int *p=x?&x:&y;return 0;}','qualifiers'),
'global_qualifier':('extern int x;const int x=1;int main(void){return x;}','conflicting'),
'qualifier_drop':('int main(void){const int x=1;int *p=&x;return 0;}','qualifiers'),
'bad_ptr':('int main(void){int *p=7;return 0;}','pointer'),
'float':('int main(void){double x=2.5;return 0;}','not implemented'),
'bitfield_type':('struct S{long x:3;};int main(void){return 0;}','bit fields require'),
'c99':('int main(void){int a=1;a++;int b=2;return a+b;}','C89'),
'clobber':('int main(void){__asm__(a6){\n nop\n}return 0;}','clobbered'),
'incomplete':('struct S;int main(void){struct S s;return 0;}','incomplete'),
'bad_macro':('#define F(a,b) a+b\nint main(void){return F(1);}','macro'),
'undefined_link':('extern int missing;int main(void){return missing;}','undefined'),
}
NEGATIVE.update({
'bits_address':('struct S{int a:3;};int main(void){struct S s;return &s.a!=0;}','address of a bit field'),
'bits_sizeof':('struct S{int a:3;};int main(void){struct S s;return sizeof(s.a);}','sizeof a bit field'),
'bits_width':('struct S{int a:17;};int main(void){return 0;}','width'),
'bits_negative':('struct S{int a:-1;};int main(void){return 0;}','width'),
'bits_zero':('struct S{int a:0;};int main(void){return 0;}','zero must be unnamed'),
'bits_duplicate':('struct S{int a:1;int a:2;};int main(void){return 0;}','duplicate'),
'bits_const':('struct S{const int a:3;};int main(void){struct S s={1};s.a=2;return 0;}','lvalue'),
'bits_excess':('struct S{unsigned int :1;int a:3;};struct S s={1,2};int main(void){return 0;}','excess'),
})

NEGATIVE.update({
'irq_result':('__interrupt__ int f(void){return 0;}int main(void){return 0;}','void (void)'),
'irq_params':('__interrupt__ void f(int x){}int main(void){return 0;}','void (void)'),
'irq_unprototyped':('__interrupt__ void f(){}int main(void){return 0;}','void (void)'),
'irq_object':('__interrupt__ int x;int main(void){return 0;}','require a function'),
'irq_call':('__interrupt__ void f(void){}int main(void){f();return 0;}','cannot be called'),
'irq_indirect_call':('typedef __interrupt__ void (*Irq)(void);__interrupt__ void f(void){}int main(void){Irq p=f;p();return 0;}','cannot be called'),
'irq_pointer':('__interrupt__ void f(void){}int main(void){void (*p)(void)=f;return 0;}','incompatible pointer'),
'irq_redeclare':('__interrupt__ void f(void);void f(void){}int main(void){return 0;}','conflicting'),
'irq_main':('__interrupt__ void main(void){}','main cannot'),
'naked_local':('__naked__ void f(void){int x;}int main(void){return 0;}','without declarations'),
'naked_return':('__naked__ void f(void){return;}int main(void){return 0;}','only accept inline'),
'naked_statement':('__naked__ void f(void){if(1){}}int main(void){return 0;}','only accept inline'),
'naked_clobber':('__naked__ void f(void){__asm__(d0){\n rts\n}}int main(void){return 0;}','omit the list'),
'naked_late':('void f(void){}__naked__ void f(void);int main(void){return 0;}','conflicting'),
})


def main():
    for name,(source,expected) in CASES.items():
        other=None
        if name=='multifile':other={'lib.c':'int shared=9;int add(int a,int b){return a+b;}'}
        if name=='asm_pc':other={'routine.s':"        xdef _get\n_get:\n        lea .data(pc),a0\n        move.w (a0),d0\n        rts\n.data:  dc.w %101010\n"}
        if name=='asm_macro_module':other={'routine.s':r"""VAL     equ 1+2<<3
ORVAL   equ 2*3!4
incval  macro
        addq.w #1,\1
        endm
        xdef _get
_get:
        move.w #VAL+ORVAL-2,d0 comment without semicolon
        incval d0
        incval d0
        rts
"""}
        if name=='asm_saved':other={'routine.s':"""        xdef _probe
        xref _worker
_probe:
        movem.l d2-d3/a2,-(sp)
        move.l #$11223344,d2
        move.l #$aabbccdd,d3
        move.l #$55667788,a2
        jsr _worker
        cmpi.l #$11223344,d2
        bne.w .fail
        cmpi.l #$aabbccdd,d3
        bne.w .fail
        cmpa.l #$55667788,a2
        bne.w .fail
        moveq #1,d0
        bra.w .done
.fail:  moveq #0,d0
.done:  movem.l (sp)+,d2-d3/a2
        rts
"""}
        if name=='asm_module':other={'routine.s':'        xdef _twice\n_twice:\n        move.w 4(sp),d0\n        add.w d0,d0\n        rts\n'}
        exe=build(name,source,other)
        header(exe)
        if run:
            for base in (0x20000,0x64a00):
                result=run(exe,base)
                assert result['exit']==expected,(name,result['exit'],expected)
                if name=='inline_trap':assert result['stdout']=='STCC OK\r\n'
        passed.append(name)
        print('PASS',name,'(execute x2)' if run else '(compile + TOS header)')
    for name,(source,word) in NEGATIVE.items():
        path=OUT/('bad_'+name+'.c');path.write_text(source)
        r=command([CC,path,'-o',OUT/'BAD.TOS'],good=False)
        assert word in r.stderr,(name,r.stderr)
        passed.append('reject_'+name);print('PASS reject',name)
    # Serialized objects must actually survive a separate compiler process.
    for name in ('multifile.c','multifile_lib.c'):
        command([CC,'-c',OUT/name,'-o',OUT/(name+'.o')])
    command([CC,OUT/'multifile.c.o',OUT/'multifile_lib.c.o','-o',OUT/'objects.TOS'])
    if run:assert run(OUT/'objects.TOS')['exit']==12
    passed.append('objects');print('PASS separate object compilation and link')
    assembler_tests()
    differential_tests()
    for text in skipped:print('SKIP',text)
    report={'passed':len(passed),'tests':passed,'skipped':skipped,'engine':'Unicorn M68000' if run else None}
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'{len(passed)} PASS; {len(skipped)} SKIP')


def assembler_tests():
    # Fixed machine words checked against Motorola instruction-format tables.
    vectors=[('nop','4e71'),('rts','4e75'),('trap #1','4e41'),
      ('moveq #-1,d0','70ff'),('move.l #$12345678,d0','203c12345678'),
      ('move.w d0,-(sp)','3f00'),('lea 8(sp),sp','4fef0008'),
      ('move.l 8(a6),-(sp)','2f2e0008'),('link a6,#-12','4e56fff4'),
      ('movem.l d2-d7/a2-a5,-(sp)','48e73f3c'),('movem.l (sp)+,d2-d7/a2-a5','4cdf3cfc'),
      ('lsl.l #1,d0','e388'),('lsr.l d1,d0','e2a8'),('addi.l #5,d0','068000000005'),
      ('muls.w d1,d0','c1c1'),('divu.w #7,d0','80fc0007'),
      ('btst #3,(a0)','08100003'),('exg d0,a1','c189'),
      ('move.w 4(a0,d1.l),d2','34301804'),('move.w (4,a0,d1.w),d2','34301004'),
      ('move.w ($ffff8240).l,d0','3039ffff8240'),('move.w $8240.w,d0','30388240'),('lea 0(pc),a0','41fa0000'),('move.l ($1234),d0','203900001234')]
    for i,(asm,wanted) in enumerate(vectors):
        # Explicit absolute short addresses $8240..$ffff must be sign-extended.
        src=OUT/f'enc{i}.s';dst=OUT/f'enc{i}.TOS';src.write_text('        '+asm+'\n')
        command([CC,'--nostartfiles',src,'-o',dst])
        b,t,d,bs,sy=header(dst)
        assert b[28:28+len(bytes.fromhex(wanted))].hex()==wanted,(asm,b[28:28+t].hex(),wanted)
        passed.append('encoding_'+str(i))
    for asm in ['move.l d0,#3','move.b a0,d0','bra.l x','move.l 0(a0,d1.l*4),d0','bkpt #1','move.b #256,d0']:
        src=OUT/'invalid.s';src.write_text('        '+asm+'\n')
        command([CC,'--nostartfiles',src,'-o',OUT/'invalid.TOS'],good=False)
        passed.append('reject_asm_'+asm)
    src=OUT/'reloc.s';src.write_text('''        section text
start:  move.l #item,d0
        bra.w done
        ds.b 600
done:   rts
        section data
item:   dc.l start,item+4
        section bss
buffer: ds.b 19
''')
    command([CC,'--nostartfiles',src,'-o',OUT/'reloc.TOS'])
    b,t,d,bs,sy=header(OUT/'reloc.TOS');assert t==612 and d==8 and bs==20
    passed.append('relocation_format')
    print('PASS assembler encoding, invalid addressing, TOS relocation layout')


def differential_tests():
    if not run:
        skipped.append('reference C differential execution (needs Unicorn)');return
    rng=random.Random(68000)
    checks=[]
    # Unsigned 32-bit arithmetic has the same semantics on the host and target.
    # Explicit masks keep unsigned long's host width out of the comparison.
    for i in range(36):
        a=rng.randrange(2**32);b=rng.randrange(1,2**32)
        op=['+','-','*','/','%','&','|','^'][i%8]
        checks.append(f' if ((({a}UL {op} {b}UL)&4294967295UL) != {eval_uint(a,b,op)}UL) return {i+1};')
    source='int main(void){\n'+'\n'.join(checks)+'\nreturn 0;}\n'
    exe=build('random_uint32',source)
    assert run(exe,limit=6000000)['exit']==0
    host=OUT/'random_uint32_host';path=OUT/'random_uint32.c'
    command(['cc','-std=c89','-pedantic-errors',path,'-o',host])
    command([host]);passed.append('36_uint32_reference_checks')
    print('PASS 36 deterministic uint32 checks on 68000 and native reference C')


def eval_uint(a,b,op):
    if op=='+':v=a+b
    elif op=='-':v=a-b
    elif op=='*':v=a*b
    elif op=='/':v=a//b
    elif op=='%':v=a%b
    elif op=='&':v=a&b
    elif op=='|':v=a|b
    else:v=a^b
    return v&0xffffffff

if __name__=='__main__':main()
