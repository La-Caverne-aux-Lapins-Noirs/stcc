#!/usr/bin/env python3
"""Independently check ROM bytes, address spaces, STCO round trips and diagnostics.
No emulator or third-party module is needed; cold-boot tests are in rom_hatari.py.
"""
import json
import os
import re
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'tests/out/rom'


def compile_rom(*args, ok=True):
    r = subprocess.run([str(os.environ.get('STCC', ROOT/'stcc')), *map(str, args)], cwd=ROOT,
                       text=True, capture_output=True)
    assert (r.returncode == 0) == ok, (args, r.stdout, r.stderr)
    return r


def symbols(path):
    return {m[2]: int(m[1], 16) for m in
            re.finditer(r'^([0-9a-f]{8}) [GL] (\S+) ', path.read_text(), re.M)}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    passed = []

    def record(name):
        passed.append(name)
        print('PASS ROM', name)

    source = OUT/'minimal.c'
    source.write_text('int main(void) { return 42; }\n')
    image, linkmap = OUT/'minimal.rom', OUT/'minimal.map'
    compile_rom('--rom', source, '-o', image, '--map', linkmap)
    raw = image.read_bytes()
    syms = symbols(linkmap)
    assert len(raw) == 196608
    assert raw[:4] == b'\x60\x2e\x01\x00'
    assert struct.unpack_from('>II', raw, 4) == (0xfc0030, 0xfc0000)
    assert raw[44:48] == b'STCC'
    assert syms['__stcc_rom_reset'] == 0xfc0030
    assert syms['__stcc_bss_end'] - syms['__stcc_bss_start'] == 16384
    used = syms['__stcc_rom_used_end'] - 0xfc0000
    assert set(raw[used:]) == {255}
    record('ST header, fixed reset vector, 16 KiB stack and exact FF-padded size')

    # A golden image computed from 68000 opcodes and the declared contributions,
    # independently of the map. Includes a fixup at offset zero, PC displacements,
    # a 16-bit RAM address, pointers stored in DATA and odd BSS alignment.
    first, second = OUT/'first.s', OUT/'second.s'
    first.write_text('''        section text
        xdef boot,fn
        xref value,scratch
boot:   dc.l fn,value,scratch
        dc.w value
        lea fn(pc),a0
        lea 6(pc),a1
        bra.w fn
fn:     rts
        section data
        dc.l fn,value,scratch
        section bss
        ds.b 3
''')
    second.write_text('''        section text
        xdef other
other:  nop
        section data
        xdef value
value:  dc.l boot,other,__stcc_data_load,__stcc_data_end
        section bss
        xdef scratch
scratch: ds.b 7
''')
    layout = ['--rom', '--nostartfiles', '--rom-base', '0xe00000',
              '--rom-size', '128', '--ram-base', '0x2000', '--ram-limit', '0x3000',
              '--entry', 'boot']
    golden = struct.pack('>IIIH', 0xe0001a, 0x200c, 0x2020, 0x200c)
    golden += bytes.fromhex('41fa000a43fa0006600000024e754e71')
    golden += struct.pack('>7I', 0xe0001a, 0x200c, 0x2020, 0xe00000,
                          0xe0001c, 0xe0001e, 0x201c)
    golden += b'\xff'*(128-len(golden))
    compile_rom(*layout, first, second, '-o', image, '--map', linkmap)
    assert image.read_bytes() == golden
    syms = symbols(linkmap)
    assert syms['__stcc_data_size'] == 28
    assert syms['__stcc_bss_size'] == 12
    assert syms['__stcc_bss_end'] == 0x2028
    record('independent golden bytes: TEXT/ROM, DATA/RAM, BSS and PC-relative fixups')
    for s in (first, second):
        compile_rom('-c', s, '-o', s.with_suffix('.o'))
        assert s.with_suffix('.o').read_bytes()[:8] == b'STCO\0\0\0\1'
    compile_rom(*layout, first.with_suffix('.o'), second.with_suffix('.o'), '-o', image)
    assert image.read_bytes() == golden
    record('unchanged STCO v1 format and identical separate-object linking')

    builtin_pc = OUT/'builtin-pc.s'
    builtin_pc.write_text('        section text\n        lea __stcc_text_end(pc),a0\n        rts\n')
    compile_rom('--rom', '--nostartfiles', '--rom-size', '64', builtin_pc, '-o', image)
    assert image.read_bytes()[:6] == bytes.fromhex('41fa00044e75')
    record('PC-relative linker address symbols retain address semantics')

    def rejected(name, args, diagnostic):
        image.write_bytes(b'previous valid image')
        r = compile_rom(*args, '-o', image, ok=False)
        assert diagnostic in r.stderr, (name, r.stderr)
        assert image.read_bytes() == b'previous valid image', name
        record(name+'; existing output preserved')

    cases = [
        ('layout without --rom', ['--ram-base', '4096'], 'require --rom'),
        ('compile-only conflict', ['--rom', '-S'], 'requires linking'),
        ('invalid number', ['--rom', '--rom-size', '192K'], 'invalid value'),
        ('negative number', ['--rom', '--ram-base', '-1'], 'invalid value'),
        ('number overflow', ['--rom', '--ram-base', '4294967296'], 'invalid value'),
        ('nonnumeric option value', ['--rom', '--ram-base'], 'invalid value'),
        ('unsupported automatic bootstrap', ['--rom', '--rom-base', '0xe00000'],
         'default ST ROM startup'),
        ('automatic screen overlap', ['--rom', '--ram-limit', '0x80000'],
         'default ST ROM startup'),
        ('empty ROM', ['--rom', '--nostartfiles', '--rom-size', '0'], 'invalid ROM layout'),
        ('odd ROM address', ['--rom', '--nostartfiles', '--rom-base', '0xfc0001'],
         'invalid ROM layout'),
        ('24-bit address overflow', ['--rom', '--nostartfiles', '--rom-base', '0xffff00'],
         'invalid ROM layout'),
        ('vector table overlap', ['--rom', '--nostartfiles', '--ram-base', '0x200'],
         'invalid ROM layout'),
        ('empty RAM range', ['--rom', '--nostartfiles', '--ram-base', '0x78000'],
         'invalid ROM layout'),
        ('ROM and RAM overlap', ['--rom', '--nostartfiles', '--rom-base', '0x1000'],
         'ROM and RAM ranges overlap'),
        ('ROM capacity', ['--rom', '--nostartfiles', '--rom-size', '2'], 'ROM overflow'),
        ('RAM capacity', ['--rom', '--ram-limit', '0x1002'], 'RAM overflow'),
    ]
    # Assembly input also works without _main when the default startup is omitted.
    for name, args, diagnostic in cases:
        rejected(name, [*args, first, second], diagnostic)

    invalid = OUT/'invalid.s'
    for name, content, args, diagnostic in [
        ('reserved linker definition', '__stcc_data_size equ 7\n        section text\n        rts\n', [],
         'reserved ROM linker symbol'),
        ('undefined reference', '        section text\n        dc.l absent\n', [], 'undefined symbol'),
        ('truncated ROM address', '        section text\nlabel: dc.w label\n', [], 'does not fit'),
        ('short RAM address sign extension',
         '        section text\n        move.w variable.w,d0\n        section data\nvariable: dc.w 1\n',
         ['--ram-base', '0x9000'], 'sign-extended absolute short'),
        ('out-of-range PC target',
         '        section text\n        lea variable(pc),a0\n        section data\nvariable: dc.w 1\n', [],
         'does not fit'),
        ('out-of-range branch', '        section text\n        bra.w last\n        ds.b 32768\nlast: rts\n', [],
         'does not fit'),
    ]:
        invalid.write_text(content)
        rejected(name, ['--rom', '--nostartfiles', *args, invalid], diagnostic)

    compile_rom('--rom', ROOT/'examples/15_rom/main.c', '-o', image)
    assert len(image.read_bytes()) == 196608
    record('standalone C video example builds without external ROM or target library')
    (OUT/'report.json').write_text(json.dumps({'passed': len(passed), 'tests': passed},
                                            indent=2)+'\n')
    print(len(passed), 'ROM checks passed')


if __name__ == '__main__':
    main()
