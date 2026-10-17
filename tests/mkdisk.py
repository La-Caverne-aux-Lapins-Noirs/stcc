#!/usr/bin/env python3
"""FAT12 round trips, allocation limits, reproducibility and CLI errors.
No emulator, mount, root privileges or external FAT tools are required.
An independent fsck/mtools check and real floppy boot live in mkdisk_hatari.py.
"""
import json
import os
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = Path(os.environ.get('STMKDISK', ROOT/'stmkdisk'))


class Fat12:
    def __init__(self, path):
        self.raw = Path(path).read_bytes()
        b = self.raw
        self.sector = struct.unpack_from('<H', b, 11)[0]
        self.cluster = self.sector*b[13]
        self.reserved = struct.unpack_from('<H', b, 14)[0]
        self.nfats = b[16]
        self.roots = struct.unpack_from('<H', b, 17)[0]
        self.sectors = struct.unpack_from('<H', b, 19)[0]
        self.spf = struct.unpack_from('<H', b, 22)[0]
        self.fat = b[self.reserved*self.sector:(self.reserved+self.spf)*self.sector]
        self.root_offset = (self.reserved+self.nfats*self.spf)*self.sector
        self.data_offset = self.root_offset + ((self.roots*32+self.sector-1)//self.sector)*self.sector
        self.limit = (len(b)-self.data_offset)//self.cluster+2
        assert len(b) == self.sectors*self.sector
        assert self.sector == 512 and b[13] == 2 and self.nfats == 2
        assert self.fat[:3] == bytes((b[21], 255, 255))
        for i in range(self.nfats):
            offset = (self.reserved+i*self.spf)*self.sector
            assert b[offset:offset+self.spf*self.sector] == self.fat
        assert sum(struct.unpack('>256H', b[:512])) & 65535 != 0x1234
        self.used = set()
        self.files = {}
        self.dirs = set()
        self._directory(b[self.root_offset:self.root_offset+self.roots*32], '', 0, 0)
        allocated = {c for c in range(2,self.limit) if self.next(c)}
        assert self.used == allocated, ('lost clusters', self.used ^ allocated)

    def next(self, cluster):
        offset = cluster*3//2
        value = int.from_bytes(self.fat[offset:offset+2], 'little')
        return value >> 4 if cluster & 1 else value & 4095

    def chain(self, start):
        result = bytearray()
        while start:
            assert 2 <= start < self.limit and start not in self.used, ('bad/crosslinked cluster',start)
            self.used.add(start)
            offset = self.data_offset+(start-2)*self.cluster
            result.extend(self.raw[offset:offset+self.cluster])
            start = self.next(start)
            if start >= 0xff8:
                return bytes(result)
        return bytes(result)

    def _directory(self, data, prefix, current, parent):
        if prefix:
            assert data[:11] == b'.          ' and data[32:43] == b'..         '
            assert struct.unpack_from('<H',data,26)[0] == current
            assert struct.unpack_from('<H',data,58)[0] == parent
        for off in range(0,len(data),32):
            entry = data[off:off+32]
            if not entry[0]:
                break
            assert entry[0] != 0xe5 and entry[11] != 15
            name = entry[:8].decode('ascii').rstrip()
            ext = entry[8:11].decode('ascii').rstrip()
            if entry[11] & 8 or name in ('.','..'):
                continue
            path = prefix+name+('.'+ext if ext else '')
            start = struct.unpack_from('<H',entry,26)[0]
            size = struct.unpack_from('<I',entry,28)[0]
            content = self.chain(start)
            if entry[11] & 16:
                assert not size
                self.dirs.add(path)
                self._directory(content,path+'/',start,current)
            else:
                assert bool(size) == bool(start)
                assert len(content) >= size
                assert path not in self.files
                self.files[path] = content[:size]


def command(args, good=True):
    result = subprocess.run([str(TOOL), *map(str,args)],capture_output=True,text=True)
    assert (result.returncode == 0) == good, (args,result.stdout,result.stderr)
    if not good:
        assert 'error:' in result.stderr
    return result


def main():
    passed = []
    def check(name):
        passed.append(name)
        print('PASS stmkdisk',name)
    with tempfile.TemporaryDirectory(prefix='stmkdisk-test-') as temporary:
        d = Path(temporary)
        source = d/'input';source.mkdir()
        (source/'small.txt').write_bytes(b'Atari\r\n')
        (source/'zero.bin').touch()
        (source/'big.bin').write_bytes(bytes(range(256))*20+b'end')
        sub = source/'nested';sub.mkdir()
        (sub/'empty').mkdir()
        for n in range(45):
            (sub/f'f{n:02}.dat').write_bytes(bytes((n,))*n)
        for size,sides,spt in ((360,1,9),(720,2,9),(800,2,10)):
            image = d/f'disk{size}.st'
            command(['-o',image,'--size',size,'--root',source,'--label','DEMO'])
            fs = Fat12(image)
            assert len(fs.raw) == size*1024
            assert struct.unpack_from('<HH',fs.raw,24) == (spt,sides)
            assert fs.raw[43:54] == b'DEMO       '
            assert fs.files['BIG.BIN'] == (source/'big.bin').read_bytes()
            assert fs.files['ZERO.BIN'] == b''
            assert fs.dirs == {'NESTED','NESTED/EMPTY'}
            for n in range(45):assert fs.files[f'NESTED/F{n:02}.DAT'] == bytes((n,))*n
            check(f'{size} KiB: FAT copies, all bytes, multi-cluster directory and dot entries')
        repeat = d/'repeat.st'
        command(['--root',source,'--label','DEMO','-o',repeat])
        assert repeat.read_bytes() == (d/'disk720.st').read_bytes()
        check('deterministic bytes independent of option order')
        mapped = d/'mapped.st'
        long_name = d/'very long source filename.txt';long_name.write_bytes(b'renamed')
        command(['-o',mapped,'--add',long_name,'DOC/LIREMOI.TXT',sub])
        fs = Fat12(mapped)
        assert fs.files['DOC/LIREMOI.TXT'] == b'renamed' and 'NESTED/EMPTY' in fs.dirs
        check('explicit destination rename and positional directory')
        program = d/'DEMO.TOS';program.write_bytes(b'\x60\x1aexample')
        auto = d/'auto.st';command(['-o',auto,'--auto',program])
        assert Fat12(auto).files == {'AUTO/DEMO.PRG':program.read_bytes()}
        check('AUTO placement and TOS-to-PRG name')
        assert command(['--version']).stdout.startswith('stmkdisk ')
        assert 'Usage:' in command(['--help']).stdout
        check('help and version')
        old = auto.read_bytes()
        command(['-o',auto],False);assert auto.read_bytes() == old
        command(['-o',auto,'--force','--add',program,'BAD/LONGFILENAME.PRG'],False)
        assert auto.read_bytes() == old
        check('failed forced rebuild preserves the previous image')
        command(['-o',auto,'--force']);assert Fat12(auto).files == {}
        check('refuse overwrite; explicit force creates an empty formatted disk')
        errors = [(['--bogus'],'unknown option'),(['-o'],'missing output'),
                  (['--add',program],'missing destination'),(['--size','1440','-o',d/'bad.st'],'bad geometry'),
                  (['-o',d/'bad.st','--label','012345678901'],'long label'),
                  (['-o',d/'bad.st','--label','BAD.LABEL'],'invalid label'),
                  (['-o',d/'bad.st',long_name],'long Atari filename'),
                  (['-o',d/'bad.st','--add',program,'../EVIL.PRG'],'parent escape'),
                  (['-o',d/'bad.st','--add',program,'/EVIL.PRG'],'absolute destination'),
                  (['-o',d/'bad.st','--auto',source/'small.txt'],'nonprogram AUTO'),
                  (['-o',d/'bad.st','--root',program],'non-directory root'),
                  (['-o',d/'bad.st',d/'missing'],'missing input')]
        for args,name in errors:
            command(args,False);assert not (d/'bad.st').exists();check('reject '+name)
        duplicate = d/'SMALL.TXT';duplicate.write_bytes(b'different')
        command(['-o',d/'bad.st',source/'small.txt',duplicate],False)
        check('reject case-insensitive collision')
        alias = d/'alias.st';os.link(program,alias)
        command(['-o',alias,'--force',program],False)
        assert program.read_bytes() == b'\x60\x1aexample'
        check('reject output/input hardlink alias')
        symbolic = d/'LINK';symbolic.symlink_to(source,target_is_directory=True)
        command(['-o',d/'bad.st',symbolic],False)
        check('reject symlinks without traversing')
        too_many = d/'many';too_many.mkdir()
        for n in range(112):(too_many/f'F{n:03}').touch()
        command(['-o',d/'bad.st','--root',too_many],False)
        check('reject root directory overflow')
        full = d/'FULL.BIN'
        full.write_bytes(b'x'*(713*1024))
        command(['-o',d/'full.st',full]);assert Fat12(d/'full.st').files['FULL.BIN'] == full.read_bytes()
        full.write_bytes(full.read_bytes()+b'x')
        command(['-o',d/'bad.st',full],False)
        check('exact disk capacity and overflow rejected')
        # Enough empty entries to extend a directory chain beyond one cluster.
        # Already covered above; no external reader shares this writer's code.
        command(['-o',repeat,'--force','--root',source,'--label','CHANGED'])
        assert repeat.read_bytes()[8:11] != (d/'disk720.st').read_bytes()[8:11]
        check('changed content changes Atari media serial')
        assert not list(d.glob('*.tmp.*'))
        check('no leftover output temporary files')
    out = ROOT/'tests/out';out.mkdir(exist_ok=True)
    (out/'mkdisk-report.json').write_text(json.dumps({'passed':len(passed),'tests':passed},indent=2)+'\n')
    print(f'{len(passed)} stmkdisk controls passed')


if __name__ == '__main__':
    main()
