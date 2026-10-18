"""Run emitted bytes in the independent Unicorn 68000 core.

This is a small TOS loader and TRAP test double, not an Atari/TOS emulator.
Hatari tests cover the real OS loading path separately.
"""
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.m68k_const import (UC_CPU_M68K_M68000, UC_M68K_REG_PC, UC_M68K_REG_SR,
    UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_A0)


def header(path):
    raw = Path(path).read_bytes()
    if len(raw) < 28:
        raise AssertionError('truncated TOS header')
    magic, text, data, bss, syms, reserved, flags, absolute = struct.unpack('>H6IH', raw[:28])
    assert magic == 0x601a and reserved == 0 and absolute == 0
    assert len(raw) >= 28 + text + data + syms + 4
    return raw, text, data, bss, syms


def run(path, basepage=0x20000, limit=3000000, command_tail=b'', environment=None,
        command_length=None, malloc_failure=False):
    raw, text, data, bss, syms = header(path)
    load = basepage + 0x100
    image = bytearray(raw[28:28+text+data])
    pos = 28+text+data+syms
    first, = struct.unpack('>I', raw[pos:pos+4])
    pos += 4
    relocs = []
    if first:
        at = first
        while True:
            assert at % 2 == 0 and at+4 <= len(image)
            relocs.append(at)
            val, = struct.unpack('>I', image[at:at+4])
            image[at:at+4] = struct.pack('>I', (val+load) & 0xffffffff)
            while True:
                delta = raw[pos]
                pos += 1
                if delta == 0:
                    break
                if delta == 1:
                    at += 254
                    continue
                at += delta
                break
            if not delta:
                break
    assert pos == len(raw), 'trailing bytes after relocation table'
    u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    u.ctl_set_cpu_model(UC_CPU_M68K_M68000)
    u.mem_map(0, 0x800000)
    u.mem_write(load, bytes(image))
    # Poison BSS: startup must actually clear it.
    u.mem_write(load+text+data, b'\xa5'*bss)
    bp = bytearray(256)
    for off, val in {0:basepage, 4:0x780000, 8:load, 12:text,
                     16:load+text,20:data,24:load+text+data,28:bss}.items():
        bp[off:off+4] = struct.pack('>I', val)
    assert len(command_tail) <= 126
    bp[0x80] = len(command_tail) if command_length is None else command_length
    bp[0x81:0x81+len(command_tail)] = command_tail
    if environment is not None:
        assert environment.endswith(b'\0\0') and len(environment) < 65536
        u.mem_write(0x10000, environment)
        bp[0x2c:0x30] = struct.pack('>I', 0x10000)
    u.mem_write(basepage, bytes(bp))
    stack=0x77fff0
    u.mem_write(stack, struct.pack('>II',0,basepage))
    u.reg_write(UC_M68K_REG_SR,0)
    u.reg_write(UC_M68K_REG_A7,stack)
    state={'exit':None,'stdout':'','shrink':False,'files':{},'instructions':0,
           'relocs':relocs,'malloc':[]}
    heap = [0x100000]
    handles={}
    def r16(at): return struct.unpack('>H',u.mem_read(at,2))[0]
    def r32(at): return struct.unpack('>I',u.mem_read(at,4))[0]
    def cstring(at):
        buf=bytearray()
        while len(buf)<65536:
            ch=u.mem_read(at,1)[0];at+=1
            if not ch: return bytes(buf).decode('latin1')
            buf.append(ch)
        raise AssertionError('unterminated TOS string')
    def hook(cpu,pc,size,unused):
        state['instructions']+=1
        ins=r16(pc)
        if ins == 0x4e41:
            sp=cpu.reg_read(UC_M68K_REG_A7)
            op=r16(sp);result=0
            if op==0x4a:
                assert r32(sp+4)==basepage
                assert r32(sp+8)>=256+text+data+bss
                state['shrink']=True
            elif op==0x4c:
                state['exit']=r16(sp+2);cpu.emu_stop();return
            elif op==0:
                state['exit']=0;cpu.emu_stop();return
            elif op==9:
                state['stdout']+=cstring(r32(sp+2))
            elif op==2:
                state['stdout']+=chr(r16(sp+2)&255)
            elif op==1:
                result=65
            elif op==0x48:
                count=r32(sp+2);state['malloc'].append(count)
                assert state['shrink'], 'argument allocation precedes Mshrink'
                assert count and heap[0]+count<0x700000
                result=0xffffffff if malloc_failure else heap[0]
                if not malloc_failure:heap[0]=(heap[0]+count+1)&~1
            elif op==0x3c:
                name=cstring(r32(sp+2));handle=len(handles)+6
                handles[handle]=name;state['files'][name]=b'';result=handle
            elif op==0x40:
                handle=r16(sp+2);count=r32(sp+4);ptr=r32(sp+8)
                state['files'][handles[handle]]+=bytes(cpu.mem_read(ptr,count));result=count
            elif op==0x3e:
                assert r16(sp+2) in handles
            else:
                raise AssertionError(f'unimplemented GEMDOS test double ${op:x}')
            cpu.reg_write(UC_M68K_REG_D0,result & 0xffffffff)
            cpu.reg_write(UC_M68K_REG_PC,pc+2)
        elif ins in (0x4e4d,0x4e4e):
            # Minimal BIOS Bconin / XBIOS Physbase test doubles.
            sp=cpu.reg_read(UC_M68K_REG_A7);op=r16(sp)
            if ins==0x4e4d and op==2: result=65
            elif ins==0x4e4e and op==2: result=0x700000
            else: raise AssertionError(f'unsupported BIOS/XBIOS call {op}')
            cpu.reg_write(UC_M68K_REG_D0,result)
            cpu.reg_write(UC_M68K_REG_PC,pc+2)
    u.hook_add(UC_HOOK_CODE,hook)
    try:
        u.emu_start(load,0x800000,count=limit)
    except Exception as e:
        pc=u.reg_read(UC_M68K_REG_PC)
        raise AssertionError(f'68000 execution failed at ${pc:x}: {e}') from e
    assert state['exit'] is not None, 'program did not terminate within instruction budget'
    assert state['shrink'], 'startup did not call Mshrink'
    state['cpu']=u
    state['load']=load
    return state

if __name__=='__main__':
    import sys
    result=run(sys.argv[1])
    print({k:v for k,v in result.items() if k not in ('cpu','files','relocs')})
