#!/usr/bin/env python3
"""Cold-boot stcc's own ROMs on Hatari ST/68000, without any external TOS.
Usage: python3 tests/rom_hatari.py [--hatari /path/to/hatari]
Only the initial RAM poison and debugger observations are injected by the test.
"""
import argparse
import json
import os
import re
import struct
import subprocess
from pathlib import Path
from rom import ROOT, compile_rom, symbols

OUT = ROOT/'tests/out/rom-hatari'

PROBE = '''
volatile unsigned long rom_result;
int initialized = 1234;
int values[3] = {5, 6, 7};
int *pointer = &initialized;
int zeros[23];
unsigned long factor = 76543UL;
unsigned long divisor = 37UL;
int square(int value) { return value * value; }
int (*function_pointer)(int) = square;
int main(void)
{
    int i;
    for (i = 0; i < 23; i++)
        if (zeros[i] != 0)
            return 1;
    if (*pointer != 1234 || values[2] != 7)
        return 2;
    *pointer = 4321;
    if (function_pointer(9) != 81)
        return 3;
    if (factor * divisor != 2832091UL)
        return 4;
    if (factor / divisor != 2068UL || factor % divisor != 27UL)
        return 5;
    zeros[22] = 99;
    rom_result = 0x53544343UL;
    return 42;
}
'''

IRQ = '''
volatile unsigned int hits;
__interrupt__ void handler(void) { hits++; }
int main(void)
{
    volatile unsigned long *vector = (volatile unsigned long *)0x8cUL;
    *vector = (unsigned long)handler;
    __asm__ { trap #3 }
    if (hits != 1)
        return 1;
    return 42;
}
'''


def execute(name, image, linkmap, hatari, monitor='rgb', memory='0', fault=False):
    directory = OUT/name
    directory.mkdir(exist_ok=True)
    syms = symbols(linkmap)
    # Fresh process = cold boot. Poison the whole writable arena so that a
    # successful DATA/BSS test cannot rely on the emulator's initial zero RAM.
    (directory/'poison.bin').write_bytes(b'\xa5'*(0x78000-0x1000))
    commands = [f'loadbin {directory}/poison.bin $1000']
    for label, kind in [('__stcc_rom_halt', 'halt'), ('__stcc_rom_fault', 'fault')]:
        commands.append(f'b pc = ${syms[label]:x} :trace :once :file {directory}/{kind}.ini')
        (directory/(kind+'.ini')).write_text(
            f'r\nsavebin {directory}/{kind}.bin $0 $80000\n'
            f'savebin {directory}/rom.bin $fc0000 $30000\ninfo video\nquit\n')
    (directory/'start.ini').write_text('\n'.join(commands)+'\n')
    args = [hatari, '--tos', str(image), '--patch-tos', 'off', '--fast-boot', 'off',
            '--machine', 'st', '--cpulevel', '0', '--cpuclock', '8',
            '--memsize', memory, '--monitor', monitor, '--vdi', 'off',
            '--sound', 'off', '--fast-forward', 'on', '--confirm-quit', 'off',
            '--run-vbls', '1500', '--parse', str(directory/'start.ini')]
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    r = subprocess.run(args, env=env, capture_output=True, text=True, timeout=35)
    log = r.stdout+r.stderr
    (directory/'hatari.log').write_text(log)
    assert r.returncode == 0, (name, log[-4000:])
    kind, other = ('fault', 'halt') if fault else ('halt', 'fault')
    assert (directory/(kind+'.bin')).is_file(), (name, 'entry not reached', log[-4000:])
    assert not (directory/(other+'.bin')).exists(), (name, 'unexpected entry')
    assert (directory/'rom.bin').read_bytes() == image.read_bytes(), 'ROM was modified'
    ram = (directory/(kind+'.bin')).read_bytes()
    assert len(ram) == 0x80000
    assert 'S=1' in log and 'IMASK=7' in log
    if not fault:
        d0 = int(re.search(r'\bD0 ([0-9A-Fa-f]{8})', log)[1], 16)
        sp = int(re.search(r'\bA7 ([0-9A-Fa-f]{8})', log)[1], 16)
        assert d0 == 42, (name, d0)
        assert sp == syms['__stcc_stack_end'], (name, sp)
        assert not re.search(r'(Bus Error|Address Error|illegal instruction)', log), log[-4000:]
    for vector in range(2, 256):
        value = struct.unpack_from('>I', ram, vector*4)[0]
        expected = syms['_handler'] if name == 'interrupt' and vector == 35 else syms['__stcc_rom_fault']
        assert value == expected, (name, vector, hex(value))
    return ram, syms, log


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--hatari', default='hatari')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    passed = []

    def run(name, source, **options):
        directory = OUT/name
        directory.mkdir(exist_ok=True)
        # Clear markers from an earlier run, without removing build/debug files.
        for filename in ('halt.bin', 'fault.bin', 'rom.bin'):
            (directory/filename).unlink(missing_ok=True)
        if isinstance(source, str):
            path = directory/'main.c'
            path.write_text(source)
        else:
            path = source
        image, linkmap = directory/'test.rom', directory/'test.map'
        compile_rom('--rom', path, '-o', image, '--map', linkmap)
        result = execute(name, image, linkmap, args.hatari, **options)
        passed.append(name)
        print('PASS Hatari ROM ST/68000', name)
        return result

    for memory in ('0', '1'):
        ram, syms, _ = run('startup-'+memory, PROBE, memory=memory)
        assert struct.unpack_from('>I', ram, syms['_rom_result'])[0] == 0x53544343
        assert struct.unpack_from('>h', ram, syms['_initialized'])[0] == 4321
        assert struct.unpack_from('>23h', ram, syms['_zeros']) == (0,)*22+(99,)

    for monitor in ('rgb', 'mono'):
        ram, _, log = run('video-'+monitor, ROOT/'examples/15_rom/main.c', monitor=monitor)
        assert 'Video base   : 0x78000' in log

        def pixel(x, y):
            stride, planes = (80, 1) if monitor == 'mono' else (160, 4)
            offset = 0x78000+y*stride+(x//16)*2*planes
            return bool(struct.unpack_from('>H', ram, offset)[0] & (0x8000 >> (x & 15)))

        # Independent expected pixels of the first S and final 4/2 glyphs. This
        # checks both planar layouts and completion of the final displayed line.
        assert not pixel(16, 16)
        assert all(pixel(x, 16) for x in range(17, 21))
        assert pixel(16, 17) and not pixel(17, 17)
        assert pixel(91, 64) and all(pixel(x, 64) for x in (95, 96, 97))
        assert not pixel(0, 0)

    ram, syms, _ = run('interrupt', IRQ)
    assert struct.unpack_from('>H', ram, syms['_hits'])[0] == 1
    run('illegal-instruction', 'int main(void) { __asm__ { illegal } return 42; }\n', fault=True)
    run('bus-error', 'int main(void) { __asm__ { move.l #1,$00000000 } return 42; }\n', fault=True)
    (OUT/'report.json').write_text(json.dumps({'machine': 'ST', 'cpu': '68000 @ 8 MHz',
                                            'external_tos': False, 'passed': len(passed),
                                            'tests': passed}, indent=2)+'\n')
    print(len(passed), 'Hatari ROM boots passed without external TOS')


if __name__ == '__main__':
    main()
