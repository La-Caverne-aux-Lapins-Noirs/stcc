#!/usr/bin/env python3
"""C startup arguments, ARGV environment protocol and target predefines."""
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'tests/out/arguments'
COMPILER = str(os.environ.get('STCC', ROOT/'stcc'))
try:
    from m68k import run as execute
except ImportError:
    execute = None


def compile_program(name, source, *options):
    path = OUT/(name+'.c')
    path.write_text(source)
    image = path.with_suffix('.TTP')
    subprocess.run([COMPILER,str(path),*map(str,options),'-o',str(image)],check=True)
    return image


def probe(arguments, tail=b'', length=None):
    strings=','.join(json.dumps(arg) for arg in arguments)
    declarations=''
    checks=''
    if length is not None:
        checks+=f'if(stcc_basepage->command_line[0]!={length}) return 7;\n'
    if tail:
        declarations='unsigned char original[]={' + ','.join(map(str,tail))+'};\n'
        checks+=f'for(i=0;i<{len(tail)};i++) if(stcc_basepage->command_line[i+1]!=original[i]) return 8;\n'
    return '''#include <stcc.h>
char *expected[]={'''+strings+'''};
int main(int argc,char *argv[]) {
int i,j;
'''+declarations+checks+f'if(argc!={len(arguments)} || argc!=stcc_argc || argv!=stcc_argv) return 1;\n'+'''
if(argv[argc]!=0) return 2;
for(i=0;i<argc;i++) {
 for(j=0;expected[i][j];j++) if(argv[i][j]!=expected[i][j]) return 3;
 if(argv[i][j]) return 4;
}
argv[0][0]=argv[0][0];
return 42;
}
'''


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    passed=[]
    def case(name, expected, tail=b'', environment=None, length=None, malloc=False):
        image=compile_program(name,probe(expected,tail,length))
        if execute:
            for base in (0x20000,0x34000):
                state=execute(image,basepage=base,command_tail=tail,environment=environment,
                              command_length=length,limit=12000000)
                assert state['exit']==42,(name,state['exit'])
                assert bool(state['malloc'])==malloc,(name,state['malloc'])
        passed.append(name);print('PASS arguments',name,'execute x2' if execute else 'compile only')
        return image

    case('empty',[''])
    case('spaces',[''],b' \t   ')
    case('plain',['','bonjour','ST'],b'bonjour ST')
    case('tabs',['','a','b','c'],b' \ta\tb  c ')
    case('quotes',['','a b','c d','','','avantmilieuapres'],
         b'''"a b" 'c d' "" '' avant"milieu"apres''')
    case('doubled-quotes',['','a"b',"c'd"],b'''"a""b" 'c''d' ''')
    case('windows-paths',['','C:\\TOOLS\\TEST.TTP','C:\\MES DOCS\\A.TXT'],
         b'C:\\TOOLS\\TEST.TTP "C:\\MES DOCS\\A.TXT"')
    case('unclosed-quote',['','a b'],b'"a b')
    case('carriage-return',['','a'],b'a\rignored')
    case('nul-terminator',['','a'],b'a\0ignored')
    case('last-byte',['','x'*126],b'x'*126)
    case('maximum-short-argc',['']+['x']*63,b'x '*63)
    case('bad-length-clamped',['','x'*126],b'x'*126,length=255)
    ignored=b'HOME=C:\\TMP\0ARGV=0\0WRONG.TTP\0ignored\0\0'
    case('argv-without-marker-ignored',['','short'],b'short',environment=ignored)
    case('marker-without-argv',['','short'],b'short',environment=b'HOME=C:\\TMP\0\0',length=127)
    case('marker-without-environment',['','short'],b'short',length=127)
    environment=b'X=1\0ARGV=ignored-value\0C:\\TOOLS\\CHILD.TTP\0space stays\0'+b'z'*300+b'\0\0'
    case('extended',['C:\\TOOLS\\CHILD.TTP','space stays','z'*300],b'short',
         environment=environment,length=127)
    args=['CHILD.TTP']+['argument'+str(i) for i in range(70)]
    environment=b'ARGV=\0'+b'\0'.join(x.encode() for x in args)+b'\0\0'
    image=case('extended-allocated',args,environment=environment,length=127,malloc=True)
    if execute:
        state=execute(image,environment=environment,command_length=127,malloc_failure=True)
        assert state['exit']==127 and state['malloc']
        passed.append('allocation-failure');print('PASS arguments allocation failure terminates with 127')
    case('empty-extended-fallback',['','short'],b'short',environment=b'ARGV=\0\0',length=127)

    source='''
#if !defined(__STCC__) || !defined(__ATARI_ST__) || !defined(__M68000__)
#error Missing compiler/platform/CPU predefines
#endif
#if __STCC_VERSION__ != 600L || __STCC_VERSION_MAJOR__ != 0 || __STCC_VERSION_MINOR__ != 6 || __STCC_VERSION_PATCH__ != 0
#error Incorrect compiler version
#endif
#if __CHAR_BIT__ != 8 || __SIZEOF_SHORT__ != 2 || __SIZEOF_INT__ != 2 || __SIZEOF_LONG__ != 4 || __SIZEOF_POINTER__ != 4
#error Incorrect target data model
#endif
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ || __ORDER_BIG_ENDIAN__ == __ORDER_LITTLE_ENDIAN__
#error Incorrect target byte order
#endif
#if defined(__linux__) || defined(__x86_64__)
#error Host macros leaked to target
#endif
char *version=__STCC_VERSION_STRING__;
int main(void){return version[0]=='0' && version[2]=='6' && sizeof(int)==2 ? 42:1;}
'''
    image=compile_program('predefines',source)
    if execute:assert execute(image)['exit']==42
    passed.append('predefines');print('PASS target predefines in #if, defined and C expressions')
    native=OUT/'portable-native'
    subprocess.run(['cc','-std=c89','-Wall','-Wextra','-Werror',str(ROOT/'examples/19_portable.c'),'-o',str(native)],check=True)
    assert subprocess.run([str(native)]).returncode==42
    passed.append('portable-host');print('PASS common source on native C compiler')
    for name,params in [('one','int argc'),('three','int argc,char **argv,char **env'),
                        ('long-argc','long argc,char **argv'),('unsigned-argc','unsigned int argc,char **argv'),
                        ('bad-argv','int argc,int **argv'),
                        ('unsigned-argv','int argc,unsigned char **argv'),('variadic','int argc,...')]:
        source=OUT/('reject-'+name+'.c');source.write_text(f'int main({params}){{return 0;}}\n')
        result=subprocess.run([COMPILER,str(source),'-o',str(OUT/'bad.TOS')],text=True,capture_output=True)
        assert result.returncode and 'main(void) or main(int, char **)' in result.stderr,(name,result.stderr)
        passed.append('reject-'+name);print('PASS reject main signature',name)
    (OUT/'report.json').write_text(json.dumps({'passed':len(passed),'cpu_execution':bool(execute),
                                            'tests':passed},indent=2)+'\n')
    if not execute:print('SKIP argument CPU execution: Unicorn unavailable')
    print(len(passed),'argument/predefine checks passed')


if __name__=='__main__':main()
