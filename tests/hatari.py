#!/usr/bin/env python3
"""Execute regression PRGs through Hatari + a user-supplied TOS ROM.
Usage: python3 tests/hatari.py --tos /path/etos192us.img [--hatari /path/hatari]
No ROM or emulator source is bundled with stcc.
"""
import argparse,json,os,shutil,struct,subprocess,tempfile
from pathlib import Path
from run import CASES, OUT, ROOT


def execute(exe,expected,hatari,tos,name,files=None):
    with tempfile.TemporaryDirectory(prefix='stcc-hatari-') as tmp:
        d=Path(tmp);shutil.copy2(exe,d/'TEST.TOS')
        (d/'start.ini').write_text(f'b GemdosOpcode = $4c :trace :once :file {d}/stop.ini\n')
        (d/'stop.ini').write_text(f"savebin {d}/exit.bin 'a7+2' 2\nquit\n")
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
        args=[hatari,'--tos',str(tos),'--machine','st','--cpulevel','0',
              '--cpuclock','8','--monitor','mono','--sound','off','--memsize','1',
              '--fast-forward','on','--confirm-quit','off','--conout','2',
              '--run-vbls','1200','--parse',str(d/'start.ini'),str(d/'TEST.TOS')]
        r=subprocess.run(args,env=env,text=True,capture_output=True,timeout=30)
        log=r.stdout+r.stderr
        logs=OUT/'hatari';logs.mkdir(exist_ok=True)
        (logs/(name+'.log')).write_text(log)
        assert r.returncode==0,(name,r.returncode,log[-3000:])
        assert (d/'exit.bin').exists(),(name,'Pterm not reached',log[-3000:])
        value,=struct.unpack('>H',(d/'exit.bin').read_bytes())
        assert value==expected,(name,value,expected,log[-2000:])
        for filename,expected_bytes in (files or {}).items():
            assert (d/filename).read_bytes()==expected_bytes,(name,filename)
        return log


def main():
    p=argparse.ArgumentParser();p.add_argument('--tos',required=True,type=Path)
    p.add_argument('--hatari',default='hatari');p.add_argument('--examples',action='store_true');a=p.parse_args()
    tos=a.tos.resolve();assert tos.is_file(),'TOS ROM not found'
    results=[]
    for name,(_,expected) in CASES.items():
        exe=OUT/(name+'.TOS');assert exe.exists(),'Run tests/run.py first'
        log=execute(exe,expected,a.hatari,tos,name)
        if name=='inline_trap':assert 'STCC OK' in log
        print('PASS Hatari ST/68000',name);results.append(name)
    examples=[]
    skips=[]
    if a.examples:
        from build_examples import EXAMPLES
        for name,_,expected in EXAMPLES:
            if name=='06KEY.TOS':
                skips.append('06KEY.TOS: blocking keyboard input; compiled and CPU test only')
                print('SKIP Hatari example',skips[-1]);continue
            files=None
            if name=='07FILE.TOS':files={'STCC.TXT':b'Ecrit par un programme compile avec stcc.\r\n'}
            execute(ROOT/'examples/bin'/name,expected,a.hatari,tos,'example_'+name,files)
            print('PASS Hatari example',name);examples.append(name)
    report={'machine' :'Atari ST','cpu':'M68000 @ 8 MHz','rom':tos.name,
            'passed':len(results),'tests':results,'examples_passed':len(examples),'examples':examples,'skipped':skips}
    (OUT/'hatari-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'{len(results)} Hatari PASS')

if __name__=='__main__':main()
