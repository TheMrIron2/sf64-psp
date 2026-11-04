"""Build and verify the three fixed GU scratchpad functions"""

import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct
from collections import Counter
import subprocess

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('assets', ROOT / 'src/psp/gfx/native_asset_compile.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)
FUNCTIONS = (
    ('backend', 'psp_gfx_gu_prepare_colored_draw_with_model', 2848),
    ('dl', 'psp_gfx_dl_resolve_effective_material_state', 660),
    ('dl', 'psp_gfx_dl_prepare_texture.isra.0', None),
)
OBJECTS = {'backend': 'src/psp/gfx/gfx_psp_backend_gu.o', 'dl': 'src/psp/gfx/gfx_psp_dl.o'}


def read(path):
    data, sections, symbols, table = assets.read_symbols(path, (1, 2, 3))
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    strings = sections[header[13]]
    names = []
    for section in sections:
        start = strings[4] + section[0]
        names.append(data[start:data.index(b'\0', start)].decode())
    return data, sections, symbols, names


def contents(obj, name):
    section = obj[1][obj[3].index(name)]
    return section, obj[0][section[4]:section[4] + section[5]] if section[1] != 8 else b''


def identical(a, b):
    for i, section in enumerate(a[1]):
        if section[2] & 2:
            other, code = contents(b, a[3][i])
            assert (section[1], section[2], section[5]) == (other[1], other[2], other[5]), a[3][i]
            assert contents(a, a[3][i])[1] == code, a[3][i]


def relocations(obj):
    for section in obj[1]:
        if section[1] != 9:
            continue
        for p in range(section[4], section[4] + section[5], 8):
            offset, info = struct.unpack_from('<II', obj[0], p)
            name, _, _, target_section = obj[2][info >> 8]
            yield section[7], offset, info & 255, name or obj[3][target_section]


def function_code(obj, name):
    address, size, index = next((a, s, i) for n, a, s, i in obj[2] if n == name)
    section = obj[1][index]
    start = section[4] + address - section[3]
    return address, size, obj[0][start:start + size]


def inspect(obj, name, expected_size):
    index = obj[3].index('.text.' + name)
    section, code = contents(obj, '.text.' + name)
    if expected_size is not None:
        assert len(code) == expected_size
    masks = [0xffffffff] * (len(code) // 4)
    relocs = []
    for target_section, offset, kind, target in relocations(obj):
        if target_section != index:
            continue
        assert kind in (4, 5, 6), (name, offset, kind, target)
        if kind in (5, 6):
            assert not target.startswith('.text') and target != name, (name, target)
        masks[offset // 4] = 0xfc000000 if kind == 4 else 0xffff0000
        relocs.append(dict(offset=offset, kind=kind, target=target))
    branches = returns = indirect_calls = 0
    words = struct.unpack('<' + 'I' * len(masks), code)
    for i, word in enumerate(words):
        offset = i * 4
        op, rs, funct = word >> 26, (word >> 21) & 31, word & 63
        if op in (2, 3):
            assert any(r['offset'] == offset and r['kind'] == 4 and
                       r['target'] not in (name, '.text.' + name) for r in relocs), (name, offset)
        if op == 0 and funct in (8, 9):
            if funct == 8:
                assert rs == 31, ('indirect return', name, offset)
                returns += 1
            else:
                assert name == 'psp_gfx_dl_prepare_texture.isra.0' and rs != 31 and ((word >> 11) & 31) == 31, \
                    ('unexpected indirect call', name, offset)
                indirect_calls += 1
        if op == 1:
            assert ((word >> 16) & 31) in (0, 1, 2, 3), ('branch and link', name, offset)
        if op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op in (17, 18) and rs == 8):
            delta = struct.unpack('<h', struct.pack('<H', word & 65535))[0] * 4
            assert 0 <= offset + 4 + delta < len(code), ('external branch', name, offset)
            branches += 1
    if indirect_calls:
        assert indirect_calls == 3 and sum(r['target'] == 'gPspGfxBackendOps' for r in relocs) >= 3
    return code, words, masks, dict(bytes=len(code), branches=branches, returns=returns,
                                    indirect_calls=indirect_calls, relocations=relocs)


def prepare(source, output):
    text = source.read_text()
    present = []
    for i, (_, name, _) in enumerate(FUNCTIONS):
        text, count = re.subn(r'(?m)^(\s*(?:jal|j)\s+)' + re.escape(name) + r'\s*$',
                             lambda m: m[1] + f'PspScratchpad_Dispatch{i}', text)
        if re.search(r'^\s*\.ent\s+' + re.escape(name) + r'\s*$', text, re.M):
            present.append(i)
            text += f'\n.globl PspScratchpad_Original{i}\n.set PspScratchpad_Original{i}, {name}\n'
        assert count == (4, 2, 4)[i] if i in present else count == 0, (name, count)
    assert present, source
    output.write_text(text)


def objects_and_header(build):
    objects = {key: read(build / path) for key, path in OBJECTS.items()}
    routes = Counter()
    for key, obj in objects.items():
        identical(read(build / f'scratchpad-{key}-original.o'), obj)
        for index, _, kind, target in relocations(obj):
            if target.startswith('PspScratchpad_Dispatch'):
                assert kind == 4
                routes[int(target[-1])] += 1
            if obj[1][index][2] & 2:
                assert target.removeprefix('.text.') not in {f[1] for f in FUNCTIONS}, target
    assert routes == Counter({0: 4, 1: 2, 2: 4}), routes
    header = 'typedef struct { const unsigned char* original; unsigned bytes, offset; '
    header += 'const unsigned* masks; const unsigned* expected; } ScratchpadCode;\n'
    entries, offset = [], 0
    for i, (key, name, size) in enumerate(FUNCTIONS):
        actual_size = function_code(objects[key], name)[1]
        if size is None:
            assert 0 < actual_size <= 1280 and actual_size % 4 == 0, (name, actual_size)
        code_size = actual_size if size is None else size
        _, words, masks, _ = inspect(objects[key], name, code_size)
        header += f'extern const unsigned char PspScratchpad_Original{i}[];\n'
        for suffix, values in (('Masks', masks), ('Expected', [w & m for w, m in zip(words, masks)])):
            header += f'static const unsigned sScratchpad{suffix}{i}[] = {{\n'
            header += ''.join('    ' + ', '.join(f'0x{v:08x}u' for v in values[p:p + 8]) + ',\n'
                              for p in range(0, len(values), 8)) + '};\n'
        entries.append(f'    {{PspScratchpad_Original{i}, {code_size}u, {offset + 64}u, sScratchpadMasks{i}, sScratchpadExpected{i}}},\n')
        offset += ((code_size + 63) & ~63) + 128
    assert offset <= 16 * 1024
    header += f'#define PSP_SCRATCHPAD_STORAGE {offset}u\n'
    header += 'static const ScratchpadCode sScratchpadCode[3] = {\n' + ''.join(entries) + '};\n'
    return objects, header


def verify(build, objects, layout_plan):
    final = read(build / 'starfox64.psp.elf')
    addresses = {n: a for n, a, _, _ in final[2]}
    assert not any(n.startswith(('ComboProbe_', 'StateProbe_')) for n in addresses)
    for obj in objects.values():
        for index, offset, _, target in relocations(obj):
            if target.startswith('PspScratchpad_Dispatch'):
                caller = obj[3][index].removeprefix('.text.')
                address, _, code = function_code(final, caller)
                word = struct.unpack_from('<I', code, offset)[0]
                destination = ((address + offset + 4) & 0xf0000000) | ((word & 0x03ffffff) << 2)
                assert destination == addresses[target], (caller, target)
    for i, (key, name, size) in enumerate(FUNCTIONS):
        expected_size = function_code(objects[key], name)[1] if size is None else size
        _, words, masks, _ = inspect(objects[key], name, expected_size)
        address, actual_size, code = function_code(final, name)
        assert actual_size == expected_size and addresses[f'PspScratchpad_Original{i}'] == address
        linked = struct.unpack('<' + 'I' * len(words), code)
        assert all(a & m == b & m for a, b, m in zip(words, linked, masks))
        _, stub_size, stub = function_code(final, f'PspScratchpad_Dispatch{i}')
        assert stub_size == 16
        instructions = struct.unpack('<4I', stub)
        assert instructions[0] & 0xffff0000 == 0x3c020000
        assert instructions[1] & 0xffff0000 == 0x8c420000
        assert instructions[2:] == (0x00400008, 0)
        assert ((instructions[0] & 65535) << 16) + struct.unpack_from('<h', stub, 4)[0] == addresses['gPspScratchpadTargets'] + i * 4
    for name in ('BeginFrame', 'Submit', 'Shutdown'):
        dis = subprocess.check_output(['psp-objdump', '-d', '--disassemble=PspGfxDeviceGu_' + name,
                                       str(build / 'starfox64.psp.elf')], text=True)
        assert '<__wrap_PspGfxGuDevice_' + name + '>' in dis, name
    dis = subprocess.check_output(['psp-objdump', '-d', '--disassemble=PspGfxDevicePspgl_BeginFrame',
                                   str(build / 'starfox64.psp.elf')], text=True)
    assert '__wrap_PspGfxGuDevice_' not in dis and 'PspGfxGuDevice_BeginFrame' not in dis
    plan = json.loads(layout_plan.read_text())
    pins = [(f['name'], c['address'] + f['offset'], f['size']) for c in plan['chunks'] for f in c['functions']]
    functions = {n: (a, s) for n, a, s, _ in final[2]}
    assert len(pins) == 198 and all(functions[n] == (a, s) for n, a, s in pins)
    retained_count = len(json.loads((build / 'retained_assets.json').read_text()))
    code_bytes = sum(function_code(objects[key], name)[1] for key, name, _ in FUNCTIONS)
    print(f'Scratchpad: {code_bytes} code bytes, ten routes, RAM fallback and 198 pins / {retained_count} retained leaves')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=('prepare', 'header', 'verify'))
    parser.add_argument('paths', type=Path, nargs='+')
    parser.add_argument('--layout-plan', type=Path, default=ROOT / 'src/psp/gfx/gfx_psp_layout.json')
    args = parser.parse_args()
    if args.command == 'prepare':
        source, output = args.paths
        prepare(source, output)
    else:
        build, = args.paths
        objects, header = objects_and_header(build)
        target = build / 'gfx_psp_scratchpad_code.h'
        if args.command == 'header':
            if not target.exists() or target.read_text() != header:
                target.write_text(header)
        else:
            assert target.read_text() == header, 'rebuild scratchpad image'
            verify(build, objects, args.layout_plan)


if __name__ == '__main__':
    main()
