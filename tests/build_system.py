#!/usr/bin/env python3
"""Check cleanup and TOS data installation in disposable project fixtures.
This never runs clean against the user's checkout or reads their ROMs.
"""
import shutil
import stat
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(root, *args):
    return subprocess.run(['make', *map(str,args)],cwd=root,check=True,
                          capture_output=True,text=True)


def seed(root, name, content=b'fixture'):
    path = root/name
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(content)
    return path


def main():
    with tempfile.TemporaryDirectory(prefix='stcc-build-system-') as temporary:
        root = Path(temporary)/'source';root.mkdir()
        shutil.copy2(ROOT/'Makefile',root/'Makefile')
        # Seed products and inputs separately so accidental deletion is visible.
        preserved = ['stcc','stmkdisk','examples/bin/DEMO.TOS','dist/stcc.deb',
                     'src/example.c','tests/example.py','scripts/example.py',
                     'runtime/start.s','runtime/args.c','tos/TOS.IMG','tos/archive/KEEP.log',
                     'tos/__pycache__/KEEP.pyc']
        removed = ['src/example.o','tests/out/subdir/test.TOS','tests/out/run.log',
                   'validation/hatari.log','validation/report.json','validation/package-info.txt',
                   'stcc-asan','stmkdisk-asan','run.log','disk.st.tmp.123456','scratch.tmp',
                   '__pycache__/root.pyc','scripts/__pycache__/cached.pyc',
                   'tests/nested/__pycache__/cached.pyc','src/leftover.tmp',
                   'examples/check.log','scripts/cache.pyo']
        for name in preserved+removed:seed(root,name,name.encode())
        run(root,'clean')
        for name in preserved:
            assert (root/name).read_bytes() == name.encode(),name
        for name in removed:assert not (root/name).exists(),name
        run(root,'clean') # Idempotent with empty/missing output directories.
        print('PASS clean removes test traces and temporaries, preserving tools, examples, packages and tos/')
        run(root,'fclean')
        for name in ('stcc','stmkdisk','examples/bin','dist'):
            assert not (root/name).exists(),name
        for name in ('src/example.c','tests/example.py','tos/TOS.IMG','tos/archive/KEEP.log'):
            assert (root/name).exists(),name
        run(root,'fclean')
        print('PASS fclean removes delivered products and preserves sources and ROM input')

        # Mark "all" as already made to exercise install's file handling without
        # compiling dummy fixture binaries. Real package smoke tests are separate.
        seed(root,'stcc');seed(root,'stmkdisk')
        seed(root,'include/fixture.h');seed(root,'docs/fixture.md')
        for name in ('stcc','stmkdisk'):seed(root,'docs/'+name+'.1')
        for name in ('README.md','LICENSE'):seed(root,name)
        images = {'TOS.IMG':b'ROM fixture\x00\xff',
                  'nested/TOS UK.ROM':bytes(range(256)),
                  '.metadata':b'opaque metadata',
                  'archive/KEEP.log':b'tos/archive/KEEP.log',
                  '__pycache__/KEEP.pyc':b'tos/__pycache__/KEEP.pyc'}
        for name,data in images.items():seed(root,'tos/'+name,data).chmod(0o600)
        (root/'tos/nested').chmod(0o700)
        (root/'tos/empty').mkdir()
        linked = seed(Path(temporary),'outside.rom',b'linked ROM bytes')
        (root/'tos/LINK.IMG').symlink_to(linked)
        images['LINK.IMG'] = b'linked ROM bytes'
        stage = Path(temporary)/'stage'
        run(root,'-o','all','install','PREFIX=/usr','DESTDIR='+str(stage))
        assert (stage/'usr/share/stcc/runtime/args.c').read_bytes()==b'runtime/args.c'
        target = stage/'usr/share/stcc/tos'
        for name,data in images.items():
            assert (target/name).read_bytes() == data,name
            assert stat.S_IMODE((target/name).stat().st_mode) == 0o644,name
        assert stat.S_IMODE((target/'nested').stat().st_mode) == 0o755
        assert (target/'empty').is_dir()
        assert not (target/'LINK.IMG').is_symlink()
        print('PASS staged install copies nested, hidden and spaced names into /usr/share/stcc/tos with readable modes')
        other = Path(temporary)/'custom'
        run(root,'-o','all','install','PREFIX=/opt/stcc','DESTDIR='+str(other))
        assert (other/'opt/stcc/share/stcc/tos/TOS.IMG').read_bytes() == images['TOS.IMG']
        print('PASS TOS installation respects PREFIX and DESTDIR')
        shutil.rmtree(root/'tos')
        empty = Path(temporary)/'without-tos'
        run(root,'-o','all','install','PREFIX=/usr','DESTDIR='+str(empty))
        assert not (empty/'usr/share/stcc/tos').exists()
        print('PASS install without optional tos/ remains valid')


if __name__ == '__main__':
    main()
