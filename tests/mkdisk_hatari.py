#!/usr/bin/env python3
"""Boot the produced .ST in drive A (no GEMDOS host-directory mapping).
The AUTO program reads across FAT/directory cluster boundaries, resolves '..',
creates a file through GEMDOS, then returns 42. Inspect the changed disk bytes.
Optional independent mtools and fsck checks are read-only.
"""
import argparse
import json
import os
import struct
import subprocess
import tempfile
from pathlib import Path
from mkdisk import Fat12, ROOT

PROGRAM = r'''
int open_file(const char *path)
{
    int handle;
    __asm__ {
        clr.w -(sp)
        move.l path,-(sp)
        move.w #$3d,-(sp)
        trap #1
        addq.l #8,sp
        move.w d0,handle
    }
    return handle;
}
int create_file(const char *path)
{
    int handle;
    __asm__ {
        clr.w -(sp)
        move.l path,-(sp)
        move.w #$3c,-(sp)
        trap #1
        addq.l #8,sp
        move.w d0,handle
    }
    return handle;
}
long read_file(int handle,long count,void *buffer)
{
    long result;
    __asm__ {
        move.l buffer,-(sp)
        move.l count,-(sp)
        move.w handle,-(sp)
        move.w #$3f,-(sp)
        trap #1
        lea 12(sp),sp
        move.l d0,result
    }
    return result;
}
long write_file(int handle,long count,const void *buffer)
{
    long result;
    __asm__ {
        move.l buffer,-(sp)
        move.l count,-(sp)
        move.w handle,-(sp)
        move.w #$40,-(sp)
        trap #1
        lea 12(sp),sp
        move.l d0,result
    }
    return result;
}
void close_file(int handle)
{
    __asm__ {
        move.w handle,-(sp)
        move.w #$3e,-(sp)
        trap #1
        addq.l #4,sp
    }
}
unsigned char buffer[5003];
int main(void)
{
    int handle, i;
    unsigned char expected=3;
    const char marker[]="FAT12 STCC OK\r\n";
    handle=open_file("A:\\DATA\\ZLAST.BIN");
    if(handle<0)return 1;
    if(read_file(handle,sizeof(buffer),buffer)!=sizeof(buffer))return 2;
    if(read_file(handle,1L,buffer)!=0)return 3;
    close_file(handle);
    for(i=0;i<sizeof(buffer);i++){
        if(buffer[i]!=expected)return 4;
        expected+=17;
    }
    handle=open_file("A:\\DATA\\..\\ROOT.TXT");
    if(handle<0 || read_file(handle,7L,buffer)!=7L)return 5;
    close_file(handle);
    if(buffer[0]!='R' || buffer[6]!='K')return 6;
    handle=create_file("A:\\RESULT.BIN");
    if(handle<0)return 7;
    if(write_file(handle,sizeof(marker)-1,marker)!=sizeof(marker)-1)return 8;
    close_file(handle);
    return 42;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tos',required=True,type=Path)
    parser.add_argument('--hatari',default='hatari')
    parser.add_argument('--fsck',help='optional path to fsck.fat')
    parser.add_argument('--mtools-dir',type=Path,help='optional directory containing mdir and mtype')
    args = parser.parse_args()
    out = ROOT/'tests/out';out.mkdir(exist_ok=True)
    report = {'geometry_KiB':[], 'no_GEMDOS_directory_mapping':True,
              'independent_fsck':bool(args.fsck),'independent_mtools':bool(args.mtools_dir)}
    with tempfile.TemporaryDirectory(prefix='stmkdisk-hatari-') as temporary:
        d = Path(temporary)
        source = d/'probe.c';source.write_text(PROGRAM)
        program = d/'PROBE.PRG'
        subprocess.run([str(ROOT/'stcc'),str(source),'-o',str(program)],check=True)
        contents = d/'DATA';contents.mkdir()
        for n in range(44):(contents/f'F{n:02}.DAT').touch()
        payload = bytes((3+17*i)&255 for i in range(5003))
        (contents/'ZLAST.BIN').write_bytes(payload)
        root_file = d/'ROOT.TXT';root_file.write_bytes(b'ROOT-OK')
        for size in (360,720,800):
            image = d/f'disk{size}.st'
            subprocess.run([str(ROOT/'stmkdisk'),'-o',str(image),'--size',str(size),
                            '--auto',str(program),str(contents),str(root_file)],check=True)
            fs = Fat12(image)
            assert fs.files['DATA/ZLAST.BIN'] == payload
            if args.fsck:
                r = subprocess.run([args.fsck,'-n','-A',str(image)],capture_output=True,text=True)
                (out/f'mkdisk-fsck-{size}.log').write_text(r.stdout+r.stderr)
                assert r.returncode == 0,(r.stdout,r.stderr)
            if args.mtools_dir:
                subprocess.run([str(args.mtools_dir/'mdir'),'-i',str(image),'::/DATA'],
                               check=True,stdout=subprocess.DEVNULL)
                r = subprocess.run([str(args.mtools_dir/'mtype'),'-i',str(image),
                                    '::/DATA/ZLAST.BIN'],capture_output=True,check=True)
                assert r.stdout == payload
            start = d/'start.ini';stop = d/'stop.ini';exit_file = d/'exit.bin'
            exit_file.unlink(missing_ok=True)
            start.write_text(f'b GemdosOpcode = $4c :trace :once :file {stop}\n')
            stop.write_text(f"savebin {exit_file} 'a7+2' 2\nquit\n")
            env = dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
            command = [args.hatari,'--configfile','/dev/null','--tos',str(args.tos.resolve()),
                       '--machine','st','--cpulevel','0','--cpuclock','8',
                       '--monitor','mono','--sound','off','--memsize','1',
                       '--fast-forward','on','--confirm-quit','off','--conout','2',
                       '--protect-floppy','off','--disk-a',str(image),
                       '--run-vbls','2400','--parse',str(start)]
            r = subprocess.run(command,env=env,capture_output=True,text=True,timeout=45)
            log = r.stdout+r.stderr
            (out/f'mkdisk-hatari-{size}.log').write_text(log)
            assert r.returncode == 0 and exit_file.exists(), log[-4000:]
            code, = struct.unpack('>H',exit_file.read_bytes())
            assert code == 42, (size,code,log[-4000:])
            changed = Fat12(image)
            assert changed.files['RESULT.BIN'] == b'FAT12 STCC OK\r\n'
            assert changed.files['DATA/ZLAST.BIN'] == payload
            report['geometry_KiB'].append(size)
            print(f'PASS Hatari floppy {size} KiB: AUTO boot, FAT12 read/write, nested directory, ..')
    report['Steem_runtime_tested'] = False
    (out/'mkdisk-hatari-report.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__ == '__main__':
    main()
