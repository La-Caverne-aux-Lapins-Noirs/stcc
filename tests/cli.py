#!/usr/bin/env python3
"""Check command modes, include search, command-line macros and diagnostics."""
import json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    out=ROOT/'tests/out/cli';out.mkdir(parents=True,exist_ok=True)
    inc=out/'inc';inc.mkdir(exist_ok=True)
    (inc/'answer.h').write_text('#define ANSWER (BASE+2)\n')
    source=out/'main.c';source.write_text('#include <answer.h>\nint main(void){return ANSWER;}\n')
    compiler=os.environ.get('STCC',str(ROOT/'stcc'))
    args=[compiler,'-I',str(inc),'-D','BASE=17',str(source)]
    r=subprocess.run(args+['-E'],capture_output=True,text=True,check=True)
    assert 'BASE' not in r.stdout and 'ANSWER' not in r.stdout
    subprocess.run(args+['-S','-o',str(out/'main.s')],check=True)
    subprocess.run([compiler,'-c',str(out/'main.s'),'-o',str(out/'main.o')],check=True)
    subprocess.run([compiler,str(out/'main.o'),'-o',str(out/'MAIN.TOS')],check=True)
    for bad in [['--not-a-real-option'],['-o'],['-S','-c',str(source)]]:
        r=subprocess.run([compiler,*bad],capture_output=True,text=True)
        assert r.returncode and 'error:' in r.stderr
    assert (out/'MAIN.TOS').read_bytes()[:2]==b'\x60\x1a'
    try:
        from m68k import run
    except ImportError:
        print('SKIP CLI output execution (Unicorn unavailable)')
    else:
        assert run(out/'MAIN.TOS')['exit']==19
    report={'preprocessing_I_D':True,'E':True,'S_c_link':True,'invalid_options_rejected':3}
    (ROOT/'tests/out/cli-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CLI -I -D -E -S -c + separate link; 3 invalid option combinations')

if __name__=='__main__':main()
