"""Extract only relocation-free x86-64 ELF fixture text and symbol offsets."""
import re
import struct
import sys
from pathlib import Path

blob = Path(sys.argv[1]).read_bytes()
if blob[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', blob, 18)[0] != 62:
    raise ValueError('expected little-endian ELF64 x86-64 object')
shoff = struct.unpack_from('<Q', blob, 40)[0]
entsize, count, names_index = struct.unpack_from('<HHH', blob, 58)
if entsize != 64:
    raise ValueError('unexpected section header size')
sections = [struct.unpack_from('<IIQQQQIIQQ', blob, shoff + i * entsize) for i in range(count)]
def section_data(index):
    s = sections[index]
    return blob[s[4]:s[4] + s[5]]
def string(data, offset):
    return data[offset:data.index(b'\0', offset)].decode('ascii')
names = section_data(names_index)
text_index = next(i for i, s in enumerate(sections) if string(names, s[0]) == '.text')
for s in sections:
    if s[1] in (4, 9) and s[7] == text_index and s[5]:
        raise ValueError('fixture text must not contain relocations')
text = section_data(text_index)
lines = ['/* Generated from freestanding x86 C; do not edit. */',
         'static const unsigned char guest_abi_text[] = {',
         ','.join(f'0x{x:02x}' for x in text), '};']
for i, s in enumerate(sections):
    if s[1] != 2:
        continue
    strings = section_data(s[6])
    for offset in range(0, s[5], s[9]):
        name, info, _, index, value, size = struct.unpack_from('<IBBHQQ', section_data(i), offset)
        if index == text_index and info & 15 == 2:
            label = string(strings, name)
            if not re.fullmatch(r'[a-z_]+', label) or value + size > len(text):
                raise ValueError('unexpected fixture symbol')
            lines.append(f'#define GUEST_{label.upper()} {value}u')
Path(sys.argv[2]).write_text('\n'.join(lines) + '\n')
