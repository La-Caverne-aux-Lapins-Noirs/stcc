#!/usr/bin/env python3
"""Small local binary package route; debian/rules is the conventional route."""
import gzip,os,shutil,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]

def run(*args,**kwargs):
    return subprocess.run([str(x) for x in args],cwd=root,check=True,**kwargs)

def main():
    # Rebuild when changing the compiled-in installation prefix.
    run('make','-B','PREFIX=/usr','all')
    arch=subprocess.check_output(['dpkg','--print-architecture'],text=True).strip()
    with tempfile.TemporaryDirectory(prefix='stcc-deb-') as td:
        stage=Path(td)
        stage.chmod(0o755)
        run('make','PREFIX=/usr','install','DESTDIR='+str(stage))
        control=stage/'DEBIAN';control.mkdir()
        deps=run('dpkg-shlibdeps','-O',stage/'usr/bin/stcc',capture_output=True,text=True).stdout
        depends=deps.strip().removeprefix('shlibs:Depends=')
        assert depends and '${' not in depends
        (control/'control').write_text(f'''Package: stcc
Version: 0.2.0-1
Section: devel
Priority: optional
Architecture: {arch}
Maintainer: EFRITS <contact@efrits.fr>
Depends: {depends}
Suggests: python3-unicorn, hatari
Description: standalone C subset compiler and Devpac assembler for Atari ST
 Original 68000, ILP16/32, integrated assembler and linker, relocatable TOS.
 Integer C89 subset: floating point and by-value aggregate calls unsupported.
''')
        doc=stage/'usr/share/doc/stcc';doc.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(root/'debian/copyright',doc/'copyright')
        (doc/'changelog.Debian.gz').write_bytes(gzip.compress((root/'debian/changelog').read_bytes(),mtime=0))
        man=stage/'usr/share/man/man1/stcc.1'
        man.with_suffix('.1.gz').write_bytes(gzip.compress(man.read_bytes(),mtime=0))
        man.unlink()
        out=root/'dist';out.mkdir(exist_ok=True)
        package=out/f'stcc_0.2.0-1_{arch}.deb'
        run('dpkg-deb','--root-owner-group','--build',stage,package)
        # Inspect the archive and test its extracted executable with support data.
        extracted=stage/'verification'
        run('dpkg-deb','-x',package,extracted)
        env=dict(os.environ,STCC_DATA=str(extracted/'usr/share/stcc'))
        for example in ('01_minimal.c','11_bits.c','12_interrupts.c','14_vbl.c'):
            run(extracted/'usr/bin/stcc',root/'examples'/example,
                '-o',stage/'verify.TOS',env=env)
            assert (stage/'verify.TOS').read_bytes()[:2]==b'\x60\x1a'
        assert (extracted/'usr/share/man/man1/stcc.1.gz').exists()
        print(package)

if __name__=='__main__':main()
