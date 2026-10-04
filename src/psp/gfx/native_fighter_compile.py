#!/usr/bin/env python3
"""Compile one PSP asset object into a specialised native drawing function."""

import argparse
import collections
import hashlib
from pathlib import Path
import re
import struct


ASSET = 'aVenomFighter1DL'


def read_asset(path):
    data = Path(path).read_bytes()
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    if header[0][:6] != b'\x7fELF\x01\x01' or header[1:3] != (1, 8):
        raise ValueError('expected a little endian MIPS ELF object')
    sections = [struct.unpack_from('<IIIIIIIIII', data, header[6] + i * header[11])
                for i in range(header[12])]
    symbols = []
    symbol_section = next(i for i, s in enumerate(sections) if s[1] == 2)
    table = sections[symbol_section]
    strings = sections[table[6]]
    for offset in range(table[4], table[4] + table[5], table[9]):
        name, value, size, info, other, index = struct.unpack_from('<IIIBBH', data, offset)
        start = strings[4] + name
        name = data[start:data.index(0, start)].decode()
        symbols.append((name, value, size, index))
    asset = next(s for s in symbols if s[0] == ASSET)
    _, start, size, index = asset
    section = sections[index]
    if size != 59 * 8 or section[1] != 1:
        raise ValueError('fighter layout changed')
    words = [struct.unpack_from('<II', data, section[4] + start + i)
             for i in range(0, size, 8)]
    relocations = {}
    for rel in sections:
        if rel[1] != 9 or rel[7] != index:
            continue
        if rel[6] != symbol_section:
            raise ValueError('unexpected relocation symbol table')
        for offset in range(rel[4], rel[4] + rel[5], rel[9]):
            address, info = struct.unpack_from('<II', data, offset)
            if start <= address < start + size:
                if (info & 255) != 2 or (address - start) % 8 != 4:
                    raise ValueError('expected R_MIPS_32 pointer relocation')
                relocations[(address - start) // 8] = symbols[info >> 8]
    return words, relocations, hashlib.sha256(data).hexdigest()


def compile_asset(words, relocations):
    lines = []
    events = []
    slots = set()
    totals = collections.Counter()
    in_triangle_run = False
    for i, (w0, w1) in enumerate(words):
        op = w0 >> 24
        totals[op] += 1
        if op in (4, 0xfd):
            if i not in relocations:
                raise ValueError('unresolved source pointer')
            name, value, size, index = relocations[i]
            if not index or not re.fullmatch(r'[A-Za-z_]\w*', name):
                raise ValueError('source pointer needs a named object')
            if op == 4:
                count, slot = (w0 >> 10) & 63, (w0 >> 17) & 127
                if not count or slot + count > 64 or w1 % 16 or w1 + count * 16 > size:
                    raise ValueError('invalid vertex load')
                slots.update(range(slot, slot + count))
                totals['vertices'] += count
                lines.append(f'    psp_gfx_dl_load_vertices(ctx, &{name}[{w1 // 16}], {count}, {slot});')
                events.append(('load', name, w1, count, slot))
            else:
                if w1 >= size:
                    raise ValueError('invalid texture reference')
                pointer = name if w1 == 0 else f'(const u8*) {name} + {w1}'
                args = (pointer, (w0 >> 21) & 7, (w0 >> 19) & 3)
                lines.append(f'    psp_gfx_dl_set_texture_image(ctx, {args[0]}, {args[1]}, {args[2]});')
                events.append(('image', *args))
        elif op == 0xf5:
            if ((w1 >> 24) & 7) == 0:
                args = ((w0 >> 21) & 7, (w0 >> 19) & 3, (w1 >> 20) & 15,
                        (w1 >> 18) & 3, (w1 >> 14) & 15, (w1 >> 8) & 3,
                        (w1 >> 4) & 15, (w1 >> 10) & 15, w1 & 15)
                lines.append('    psp_gfx_dl_set_render_tile(ctx, ' + ', '.join(map(str, args)) + ');')
                events.append(('tile', *args))
        elif op == 0xf2:
            if ((w1 >> 24) & 7) == 0:
                args = ((w0 >> 12) & 4095, w0 & 4095, (w1 >> 12) & 4095, w1 & 4095)
                lines.append('    psp_gfx_dl_set_render_tile_size(ctx, ' + ', '.join(map(str, args)) + ');')
                events.append(('size', *args))
        elif op in (0xbf, 0xb1):
            indices = []
            for word in ([w0, w1] if op == 0xb1 else [w1]):
                for shift in (16, 8, 0):
                    encoded = (word >> shift) & 255
                    slot = encoded // 2
                    if encoded & 1 or slot not in slots:
                        raise ValueError('invalid triangle slot')
                    indices.append(slot)
            totals['triangles'] += len(indices) // 3
            lines.append(f'    psp_gfx_dl_native_tri{2 if op == 0xb1 else 1}(ctx, ' +
                         ', '.join(map(str, indices)) + ', &triangleProjectionSerial);')
            events.append(('tri2' if op == 0xb1 else 'tri1', *indices))
        elif op == 0xb8:
            if i != len(words) - 1 or w0 != 0xb8000000 or w1:
                raise ValueError('invalid list end')
        elif op not in (0xe6, 0xe8, 0xf3):
            raise ValueError(f'unsupported opcode {op:02x}')
        if op not in (0xbf, 0xb1) and in_triangle_run:
            lines.append('    triangleProjectionSerial = 0;')
        in_triangle_run = op in (0xbf, 0xb1)
    if set(relocations) != {i for i, (w0, _) in enumerate(words) if w0 >> 24 in (4, 0xfd)}:
        raise ValueError('unexpected relocation')
    if (totals[4], totals['vertices'], totals['triangles'], totals[0xbf], totals[0xb1], totals[0xb8]) != (6, 60, 24, 2, 11, 1):
        raise ValueError('fighter coverage changed')
    histogram = ['#if PROFILE_PHASES']
    for op, count in sorted((k, v) for k, v in totals.items() if isinstance(k, int)):
        histogram.extend(f'    PspProfiler_CountOpcode(0x{op:02x});' for _ in range(count))
    histogram.append('#endif')
    return '\n'.join(histogram + lines), events


def generate(path):
    words, relocations, fingerprint = read_asset(path)
    body, _ = compile_asset(words, relocations)
    return f'''// Generated from the PSP asset object by native_fighter_compile.py
// Object SHA256 {fingerprint}
#include "assets/ast_enmy_planet.h"

#define PSP_NATIVE_FIGHTER_COMMANDS 59

static void psp_gfx_dl_native_fighter(PspGfxDlContext* ctx) {{
    u32 triangleProjectionSerial = 0;

{body}
}}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('object')
    parser.add_argument('output')
    args = parser.parse_args()
    result = generate(args.object)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != result:
        output.write_text(result, newline='\n')


if __name__ == '__main__':
    main()
