#!/usr/bin/env python3
"""Inventory PSP asset objects or compile reviewed assets into native drawing calls"""

import argparse
import bisect
import collections
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess

MANIFEST_PATH = Path(__file__).with_name('native_assets.json')


def load_manifest(path):
    return validate_manifest(json.loads(Path(path).read_text()))


def validate_manifest(manifest):
    if (not isinstance(manifest, dict) or manifest.get('version') != 1 or
            not isinstance(manifest.get('objects'), list) or not manifest['objects']):
        raise ValueError('expected a version 1 asset manifest')
    assets = {}
    sources = set()
    for group in manifest['objects']:
        if (not isinstance(group, dict) or not isinstance(group.get('source'), str) or
                not isinstance(group.get('assets'), list)):
            raise ValueError('invalid asset object')
        source = group['source']
        parts = Path(source).parts
        if (len(parts) != 4 or parts[:2] != ('src', 'assets') or
                not re.fullmatch(r'[A-Za-z_]\w*', parts[2]) or parts[3] != parts[2] + '.c' or
                group.get('header') != 'assets/' + parts[2] + '.h' or source in sources or not group['assets']):
            raise ValueError('invalid or duplicate asset object')
        sources.add(source)
        for name in group['assets']:
            if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z_]\w*', name) or name in assets:
                raise ValueError('invalid or duplicate asset name')
            suffix = 'fighter' if name == 'aVenomFighter1DL' else 'asset_' + name
            assets[name] = (group['header'], suffix)
    return manifest, assets


MANIFEST, ASSETS = load_manifest(MANIFEST_PATH)
ACCEPTED_MANIFEST, ACCEPTED_ASSETS = MANIFEST, ASSETS.copy()
BASE_MANIFEST, BASE_ASSETS = MANIFEST, ASSETS.copy()
COVERAGE_MANIFEST = {'version': 1, 'objects': []}
COVERAGE_ASSETS = set()


def select_coverage(path, names=None):
    global MANIFEST, ASSETS, BASE_MANIFEST, BASE_ASSETS, COVERAGE_MANIFEST, COVERAGE_ASSETS
    inventory = json.loads(Path(path).read_text())
    policy = inventory.get('selection_policy', {})
    if inventory.get('version') != 1 or policy.get('version') != 1:
        raise ValueError('expected a version 1 corpus selection policy')
    rows = policy.get('assets', [])
    policy_names = [r['name'] for r in rows]
    if len(policy_names) != len(set(policy_names)):
        raise ValueError('duplicate selection policy name')
    accepted = {r['name'] for r in rows if r['status'] == 'accepted'}
    if accepted != ACCEPTED_ASSETS.keys():
        raise ValueError('selection policy does not match the accepted baseline')
    candidates = [r for r in rows if r['status'] == 'validation_candidate']
    if names is not None:
        requested = set(names)
        if not requested or len(requested) != len(names):
            raise ValueError('expected unique candidate names')
        if not requested <= {r['name'] for r in candidates}:
            raise ValueError('selection contains an unqualified candidate')
        candidates = [r for r in candidates if r['name'] in requested]
    supported = {r['name']: r for r in inventory['assets']}
    if set(policy_names) != supported.keys():
        raise ValueError('selection policy does not cover the saved corpus')
    decisions = {r['name']: r for r in inventory['mutation_audit']['decisions']}
    objects = {r['source']: r for r in inventory['objects']}
    groups = collections.defaultdict(list)
    for row in candidates:
        name, source = row['name'], row['source']
        asset, decision = supported[name], decisions[name]
        if (name in accepted or row['audit_status'] != 'direct_reads_only' or
                asset['status'] != 'supported' or asset['source'] != source or
                asset['enabled'] or decision['status'] != 'direct_reads_only' or
                not decision['supported'] or decision['enabled'] or not decision['source_uses'] or
                decision['writer_uses'] or decision['alias_uses'] or decision['uncovered_sources']):
            raise ValueError('candidate lacks supported direct-read evidence ' + name)
        groups[source].append(name)
    extra = {'version': 1, 'objects': [dict(source=source, header=objects[source]['header'], assets=names)
                                      for source, names in sorted(groups.items())]}
    validate_manifest(extra)
    relevant = set(groups) | {'include/' + objects[source]['header'] for source in groups}
    candidate_names = {r['name'] for r in candidates}
    relevant |= {u['source'] for u in inventory['mutation_audit']['uses'] if u['name'] in
                 candidate_names and not u['source'].startswith('src/psp/gfx/')}
    relevant.add('include/PR/gbi.h')
    fingerprints = {r['source']: r['sha256'] for r in inventory['mutation_audit']['files']}
    fingerprints.update({'include/' + r['header']: r['header_sha256'] for r in inventory['objects']})
    for source in relevant:
        if hashlib.sha256(Path(source).read_bytes()).hexdigest() != fingerprints.get(source):
            raise ValueError('stale coverage evidence for ' + source)
    union = {g['source']: dict(g, assets=list(g['assets'])) for g in ACCEPTED_MANIFEST['objects']}
    for group in extra['objects']:
        if group['source'] in union:
            union[group['source']]['assets'].extend(group['assets'])
        else:
            union[group['source']] = group
    MANIFEST = {'version': 1, 'objects': list(union.values())}
    ASSETS = dict(ACCEPTED_ASSETS)
    ASSETS.update({n: (g['header'], 'asset_' + n) for g in extra['objects'] for n in g['assets']})
    BASE_MANIFEST = ACCEPTED_MANIFEST if names is None else MANIFEST
    BASE_ASSETS = dict(ACCEPTED_ASSETS if names is None else ASSETS)
    COVERAGE_MANIFEST = extra if names is None else {'version': 1, 'objects': []}
    COVERAGE_ASSETS = candidate_names if names is None else set()


def read_symbols(path, types=(1,)):
    data = Path(path).read_bytes()
    if len(data) < 52:
        raise ValueError('truncated ELF header')
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    if header[0][:6] != b'\x7fELF\x01\x01' or header[1] not in types or header[2] != 8:
        raise ValueError('expected a little endian MIPS ELF file')
    if header[11] != 40 or not header[12] or header[6] + header[11] * header[12] > len(data):
        raise ValueError('invalid ELF section table')
    sections = [struct.unpack_from('<IIIIIIIIII', data, header[6] + i * header[11])
                for i in range(header[12])]
    symbols = []
    symbol_tables = [i for i, s in enumerate(sections) if s[1] == 2]
    if len(symbol_tables) != 1:
        raise ValueError('expected one symbol table')
    symbol_section = symbol_tables[0]
    table = sections[symbol_section]
    if table[9] != 16 or table[5] % 16 or not 0 < table[6] < len(sections):
        raise ValueError('invalid ELF symbol table')
    strings = sections[table[6]]
    if any(s[4] + s[5] > len(data) for s in sections if s[1] != 8):
        raise ValueError('truncated ELF section')
    for offset in range(table[4], table[4] + table[5], table[9]):
        name, value, size, info, other, index = struct.unpack_from('<IIIBBH', data, offset)
        if name >= strings[5]:
            raise ValueError('invalid symbol name')
        start = strings[4] + name
        name = data[start:data.index(0, start, strings[4] + strings[5])].decode()
        symbols.append((name, value, size, index))
    return data, sections, symbols, symbol_section


def read_asset(path, asset):
    data, sections, symbols, symbol_section = read_symbols(path)
    return decode_asset(data, sections, symbols, symbol_section, asset)


def decode_asset(data, sections, symbols, symbol_section, asset):
    words, relocations = decode_references(data, sections, symbols, symbol_section, asset)
    for _, value, extent, target_section in relocations.values():
        if (not 0 < target_section < len(sections) or sections[target_section][1] != 1
                or not extent or value + extent > sections[target_section][5]):
            raise ValueError('pointer source must be defined in this object')
    return words, relocations, hashlib.sha256(data).hexdigest()


def decode_references(data, sections, symbols, symbol_section, asset):
    matches = [s for s in symbols if s[0] == asset]
    if len(matches) != 1:
        raise ValueError('asset symbol must resolve once')
    _, start, size, index = matches[0]
    if not 0 < index < len(sections):
        raise ValueError('asset must be defined in this object')
    section = sections[index]
    if not size or size % 8 or section[1] != 1 or start + size > section[5]:
        raise ValueError('invalid asset extent')
    words = [struct.unpack_from('<II', data, section[4] + start + i)
             for i in range(0, size, 8)]
    relocations = {}
    for rel in sections:
        if rel[1] != 9 or rel[7] != index:
            continue
        if rel[6] != symbol_section or rel[9] != 8 or rel[5] % 8:
            raise ValueError('unexpected relocation table')
        for offset in range(rel[4], rel[4] + rel[5], rel[9]):
            address, info = struct.unpack_from('<II', data, offset)
            if start <= address < start + size:
                if (info & 255) != 2 or (address - start) % 8 != 4:
                    raise ValueError('expected R_MIPS_32 pointer relocation')
                command = (address - start) // 8
                if command in relocations or info >> 8 >= len(symbols):
                    raise ValueError('invalid pointer relocation')
                relocations[command] = symbols[info >> 8]
    return words, relocations


def compile_asset(words, relocations):
    if not words or words[-1] != (0xb8000000, 0):
        raise ValueError('list must end with ENDDL')
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
        elif op not in (0xe6, 0xe7, 0xe8, 0xf3):
            raise ValueError(f'unsupported opcode {op:02x}')
        if op not in (0xbf, 0xb1) and in_triangle_run:
            lines.append('    triangleProjectionSerial = 0;')
        in_triangle_run = op in (0xbf, 0xb1)
    if set(relocations) != {i for i, (w0, _) in enumerate(words) if w0 >> 24 in (4, 0xfd)}:
        raise ValueError('unexpected relocation')
    histogram = ['#if PROFILE_PHASES']
    for op, count in sorted((k, v) for k, v in totals.items() if isinstance(k, int)):
        histogram.extend(f'    PspProfiler_CountOpcode(0x{op:02x});' for _ in range(count))
    histogram.append('#endif')
    return '\n'.join(histogram + lines), events


def asset_interface(header, asset, words, relocations):
    declarations = dict((name, kind) for kind, name in re.findall(
        r'extern (Gfx|Vtx|u8|u16|u32|u64) (\w+)\[[^\]]*\];',
        (Path(__file__).resolve().parents[3] / 'include' / header).read_text()))
    references = {asset} | {r[0] for r in relocations.values()}
    if references - declarations.keys():
        raise ValueError('missing asset interface declarations')
    if declarations[asset] != 'Gfx' or any(words[i][0] >> 24 == 4 and declarations[r[0]] != 'Vtx'
                                          for i, r in relocations.items()):
        raise ValueError('unexpected display list or vertex interface type')
    return '\n'.join(f'extern {declarations[name]} {name}[];' for name in sorted(references))


def generate(path, asset):
    if asset not in ASSETS:
        raise ValueError('asset is not allowlisted')
    header, suffix = ASSETS[asset]
    words, relocations, fingerprint = read_asset(path, asset)
    body, events = compile_asset(words, relocations)
    prefix = 'PSP_NATIVE_' + suffix.upper()
    vertex_commands = sum(e[0] == 'load' for e in events)
    triangle_commands = sum(e[0] in ('tri1', 'tri2') for e in events)
    interface = asset_interface(header, asset, words, relocations)
    return f'''// Generated from the PSP asset object by native_asset_compile.py
// Object SHA256 {fingerprint}
{interface}

#define {prefix}_COMMANDS {len(words)}
#define {prefix}_VERTEX_COMMANDS {vertex_commands}
#define {prefix}_TRIANGLE_COMMANDS {triangle_commands}

static void psp_gfx_dl_native_{suffix}(PspGfxDlContext* ctx) {{
    u32 triangleProjectionSerial = 0;

{body}
}}
'''


def ordered_assets(path, names):
    _, _, symbols, _ = read_symbols(path)
    definitions = {name: (section, value) for name, value, size, section in symbols if size and section}
    if any(name not in definitions for name in names):
        raise ValueError('manifest asset is missing from object')
    return sorted(names, key=definitions.__getitem__)


def generate_tree(names, indent=4, coverage_ab=True):
    if not names:
        return ''
    middle = len(names) // 2
    asset = names[middle]
    _, suffix = ASSETS[asset]
    prefix = 'PSP_NATIVE_' + suffix.upper()
    padding = ' ' * indent
    lines = [f'{padding}if (child == {asset}) {{',
             f'{padding}    if (!psp_gfx_dl_native_asset_eligible(ctx, depth, {prefix}_COMMANDS)) {{',
             f'{padding}        return 0;', f'{padding}    }}']
    if coverage_ab and asset in COVERAGE_ASSETS:
        lines.extend([f'{padding}    return psp_gfx_dl_native_coverage_run(ctx, depth, {prefix}_COMMANDS,',
                      f'{padding}                                  {prefix}_VERTEX_COMMANDS, {prefix}_TRIANGLE_COMMANDS,',
                      f'{padding}                                  psp_gfx_dl_native_{suffix});'])
    else:
        lines.extend([f'{padding}    psp_gfx_dl_native_asset_run(ctx, depth, {prefix}_COMMANDS,',
                      f'{padding}                                {prefix}_VERTEX_COMMANDS, {prefix}_TRIANGLE_COMMANDS,',
                      f'{padding}                                psp_gfx_dl_native_{suffix});',
                      f'{padding}    return 1;'])
    lines.append(f'{padding}}}')
    if len(names) > 1:
        lines.append(f'{padding}if ((uintptr_t) child < (uintptr_t) {asset}) {{')
        lines.append(generate_tree(names[:middle], indent + 4, coverage_ab))
        lines.append(f'{padding}}} else {{')
        lines.append(generate_tree(names[middle + 1:], indent + 4, coverage_ab))
        lines.append(f'{padding}}}')
    return '\n'.join(lines)


def asset_object_order():
    source = (Path(__file__).resolve().parents[3] / 'src/psp/sources.mk').read_text()
    names = re.findall(r'^\s+(src/assets/[A-Za-z_0-9]+/[A-Za-z_0-9]+\.c)\s*', source, re.M)
    if not names or len(names) != len(set(names)):
        raise ValueError('expected unique asset sources in PSP link order')
    return {name: index for index, name in enumerate(names)}


def ordered_groups(manifest):
    order = asset_object_order()
    if any(group['source'] not in order for group in manifest['objects']):
        raise ValueError('selected asset object is missing from PSP link order')
    return sorted(manifest['objects'], key=lambda group: order[group['source']])


def generate_object_tree(groups, indent=4):
    padding = ' ' * indent
    if not groups:
        return padding + 'return 0;'
    middle = len(groups) // 2
    suffix, first, last = groups[middle]
    return '\n'.join([
        f'{padding}if ((uintptr_t) child < (uintptr_t) {first}) {{',
        generate_object_tree(groups[:middle], indent + 4),
        f'{padding}}} else if ((uintptr_t) child > (uintptr_t) {last}) {{',
        generate_object_tree(groups[middle + 1:], indent + 4),
        f'{padding}}} else {{',
        f'{padding}    return psp_gfx_dl_native_{suffix}_dispatch(ctx, child, depth);',
        f'{padding}}}'])


def generate_group(build, manifest, namespace, lookup_manifest=None, coverage_ab=True):
    output = []
    groups = []
    report = []
    selected_groups = ordered_groups(manifest) if namespace else manifest['objects']
    def append_object_dispatch(group):
        path = Path(build) / Path(group['source']).with_suffix('.o')
        names = ordered_assets(path, group['assets'])
        names = [name for name in names if name != 'aVenomFighter1DL']
        if not names:
            return
        suffix = namespace + Path(group['source']).stem
        output.append(f'''static int psp_gfx_dl_native_{suffix}_dispatch(PspGfxDlContext* ctx, const Gfx* child, u32 depth) {{
''' + generate_tree(names, coverage_ab=coverage_ab) + '''
    return 0;
}
''')
        groups.append((suffix, names[0], names[-1]))

    for group in selected_groups:
        path = Path(build) / Path(group['source']).with_suffix('.o')
        names = ordered_assets(path, group['assets'])
        for name in names:
            output.append(generate(path, name))
            words, relocations, fingerprint = read_asset(path, name)
            _, events = compile_asset(words, relocations)
            report.append({'name': name, 'source': group['source'], 'object_sha256': fingerprint,
                           'commands': len(words), 'vtx': sum(e[0] == 'load' for e in events),
                           'loaded': sum(e[3] for e in events if e[0] == 'load'),
                           'triangles': sum(2 if e[0] == 'tri2' else 1 for e in events
                                            if e[0] in ('tri1', 'tri2'))})
        if lookup_manifest is None:
            append_object_dispatch(group)
    if lookup_manifest is not None:
        for group in ordered_groups(lookup_manifest):
            append_object_dispatch(group)
    branches = []
    for suffix, first, last in groups:
        branches.append(f'''    if ((uintptr_t) child >= (uintptr_t) {first} && (uintptr_t) child <= (uintptr_t) {last}) {{
        return psp_gfx_dl_native_{suffix}_dispatch(ctx, child, depth);
    }}''')
    dispatch = '\n'.join(branches) + '\n    return 0;'
    if namespace:
        dispatch = f'''    if ((uintptr_t) child < (uintptr_t) {groups[0][1]} ||
        (uintptr_t) child > (uintptr_t) {groups[-1][2]}) {{
        return 0;
    }}
''' + generate_object_tree(groups)
    output.append('''static int psp_gfx_dl_native_''' + (namespace.rstrip('_') or 'asset') + '''_dispatch(PspGfxDlContext* ctx, const Gfx* child, u32 depth) {
#if PSP_RENDERER_DIAGNOSTICS
    if (ctx->traceActive) {
        return 0;
    }
#endif
    if (depth >= PSP_GFX_DL_MAX_DEPTH) {
        return 0;
    }
''' + dispatch + '''
}
''')
    return '\n'.join(output), report


def generate_all(build, coverage_ab=True):
    if COVERAGE_ASSETS and not coverage_ab:
        return generate_group(build, MANIFEST, 'asset_', coverage_ab=False)
    output, report = generate_group(build, BASE_MANIFEST, '')
    if COVERAGE_ASSETS:
        extra, rows = generate_group(build, COVERAGE_MANIFEST, 'coverage_', MANIFEST)
        output += '\n#if PSP_NATIVE_COVERAGE_AB\n' + extra + '\n#endif\n'
        report.extend(dict(row, coverage_candidate=True) for row in rows)
    return output, report


def verify_elf(build, elf):
    _, _, symbols, _ = read_symbols(elf, (2, 3))
    linked = {name: (value, size) for name, value, size, section in symbols if size and section}
    intervals = []
    for group in MANIFEST['objects']:
        path = Path(build) / Path(group['source']).with_suffix('.o')
        names = ordered_assets(path, group['assets'])
        if any(name not in linked for name in names):
            raise ValueError('manifest asset is missing from linked ELF')
        for first, second in zip(names, names[1:]):
            if linked[first][0] + linked[first][1] > linked[second][0]:
                raise ValueError('linked asset order changed in ' + group['source'])
        intervals.append((linked[names[0]][0], linked[names[-1]][0] + linked[names[-1]][1]))
    intervals.sort()
    if any(first[1] > second[0] for first, second in zip(intervals, intervals[1:])):
        raise ValueError('linked asset object ranges overlap')
    if COVERAGE_ASSETS:
        ranges = []
        for group in ordered_groups(MANIFEST):
            names = ordered_assets(Path(build) / Path(group['source']).with_suffix('.o'), group['assets'])
            ranges.append((linked[names[0]][0], linked[names[-1]][0] + linked[names[-1]][1]))
        if any(first[1] > second[0] for first, second in zip(ranges, ranges[1:])):
            raise ValueError('linked selected object order differs from PSP source order')
    print(f'AOT linked order verified for {len(ASSETS)} leaves')


def symbol_record(source, sections, symbol, kind=None):
    name, value, size, index = symbol
    if not 0 < index < len(sections) or not size:
        return None
    section = sections[index]
    if section[1] not in (1, 8) or value + size > section[5]:
        return None
    return {'name': name, 'source': source, 'kind': kind, 'bytes': size,
            'storage': 'data' if section[1] == 1 else 'zero_fill',
            'writable': bool(section[2] & 1)}


def reference_index(snapshots):
    interfaces = {}
    for source, (data, sections, symbols, symbol_section, interface) in snapshots.items():
        for kind, name in re.findall(r'extern (Gfx|Vtx|u8|u16|u32|u64) (\w+)\[[^\]]*\];', interface):
            if name in interfaces and interfaces[name] != kind:
                raise ValueError('conflicting asset interface types for ' + name)
            interfaces[name] = kind
    definitions = collections.defaultdict(list)
    for source, (data, sections, symbols, symbol_section, interface) in snapshots.items():
        for symbol in symbols:
            if symbol[0] in interfaces:
                node = symbol_record(source, sections, symbol, interfaces[symbol[0]])
                if node:
                    definitions[symbol[0]].append(node)
    references = []
    errors = []
    for source, (data, sections, symbols, symbol_section, interface) in snapshots.items():
        for asset in re.findall(r'extern Gfx (\w+)\[[^\]]*\];', interface):
            try:
                words, relocations = decode_references(data, sections, symbols, symbol_section, asset)
            except ValueError as error:
                errors.append({'name': asset, 'source': source, 'reason': str(error)})
                continue
            pointer_commands = {i for i, (w0, _) in enumerate(words) if w0 >> 24 in (4, 6, 0xfd)}
            for command in sorted(pointer_commands | relocations.keys()):
                w0, w1 = words[command]
                opcode = w0 >> 24
                row = {'asset': asset, 'source': source, 'command': command,
                       'opcode': f'{opcode:02x}', 'addend': w1,
                       'role': {4: 'vertices', 6: 'display_list', 0xfd: 'texture'}.get(opcode, 'other')}
                if command not in relocations:
                    row.update(target=None, resolution='unrelocated')
                else:
                    symbol = relocations[command]
                    row['target'] = symbol[0]
                    local = symbol_record(source, sections, symbol, interfaces.get(symbol[0]))
                    if local:
                        row.update(resolution='local', definition=local)
                    elif symbol[3] == 0 and symbol[0] in definitions:
                        targets = definitions[symbol[0]]
                        if len(targets) == 1:
                            row.update(resolution='cross_object', definition=targets[0])
                        else:
                            row.update(resolution='ambiguous', definitions=targets)
                    else:
                        row['resolution'] = 'unresolved'
                if 'definition' in row:
                    row['within_extent'] = w1 < row['definition']['bytes']
                    if opcode == 4:
                        count = (w0 >> 10) & 63
                        row['within_extent'] = bool(count and w1 % 16 == 0 and
                                                    w1 + count * 16 <= row['definition']['bytes'])
                    elif opcode == 6:
                        row['within_extent'] = w1 % 8 == 0 and w1 + 8 <= row['definition']['bytes']
                references.append(row)
    nodes = [node for name in sorted(definitions) for node in definitions[name]]
    resolutions = collections.Counter(row['resolution'] for row in references)
    return {'summary': {'defined_arrays': len(nodes), 'pointer_references': len(references),
                        'resolutions': dict(sorted(resolutions.items())), 'decode_errors': len(errors),
                        'writable_command_arrays': sum(n['kind'] == 'Gfx' and n['writable'] for n in nodes)},
            'definitions': nodes, 'references': references, 'errors': errors}


def command_users(build, command_names):
    root = Path(__file__).resolve().parents[3]
    users = collections.defaultdict(collections.Counter)
    sites = collections.defaultdict(list)
    objects = []
    for path in sorted((Path(build) / 'src').rglob('*.o')):
        data, sections, symbols, symbol_section = read_symbols(path)
        relative = path.relative_to(build)
        source = next((str(relative.with_suffix(ext)) for ext in ('.c', '.S', '.s')
                       if (root / relative.with_suffix(ext)).is_file()), str(relative))
        objects.append({'object': str(relative), 'source': source,
                        'object_sha256': hashlib.sha256(data).hexdigest()})
        for rel in sections:
            if rel[1] != 9:
                continue
            if (rel[6] != symbol_section or rel[9] != 8 or rel[5] % 8 or
                    not 0 < rel[7] < len(sections)):
                raise ValueError('unexpected relocation table in ' + str(path))
            section = rel[7]
            if not sections[section][2] & 2:
                continue
            owners = [(name, start, size) for name, start, size, index in symbols
                      if name and size and index == section]
            for offset in range(rel[4], rel[4] + rel[5], rel[9]):
                address, info = struct.unpack_from('<II', data, offset)
                if info >> 8 >= len(symbols) or address >= sections[section][5]:
                    raise ValueError('invalid relocation in ' + str(path))
                target = symbols[info >> 8][0]
                if target not in command_names:
                    continue
                matches = [name for name, start, size in owners if start <= address < start + size]
                owner = matches[0] if len(matches) == 1 else None
                role = 'code' if sections[section][2] & 4 else 'data'
                key = (target, source, owner, role)
                users[key][f'{info & 255:02x}'] += 1
                sites[key].append({'section': section, 'offset': address, 'type': f'{info & 255:02x}'})
    rows = [{'asset': asset, 'source': source, 'owner': owner, 'role': role,
             'relocations': dict(sorted(counts.items())), 'sites': sites[(asset, source, owner, role)]}
            for (asset, source, owner, role), counts in users.items()]
    return {'scope': 'named relocations in built project source objects with indirect aliases unverified',
            'summary': {'objects': len(objects), 'users': len(rows), 'referenced_commands': len({r['asset'] for r in rows}),
                        'unowned_users': sum(r['owner'] is None for r in rows)},
            'objects': objects, 'users': rows}


def source_uses(text, names, writer_arguments):
    pattern = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|'
                         r'[A-Za-z_]\w*|\d[\w.]*|->|<<=|>>=|\+\+|--|[=!<>+*/%&|^-]=|[^\s]', re.S)
    lines = [m.start() for m in re.finditer('\n', text)]
    tokens = [(m.group(), m.start()) for m in pattern.finditer(text)
              if not m.group().startswith(('//', '/*', '"', "'"))]
    words = [token[0] for token in tokens]
    stack = []
    groups = {}
    matches = []
    for i, word in enumerate(words):
        if word in ('(', '[', '{'):
            group = {'open': i, 'delimiter': word, 'commas': []}
            if word == '(' and i and re.fullmatch(r'[A-Za-z_]\w*', words[i - 1]):
                group['name'] = words[i - 1]
            stack.append(group)
            groups[i] = group
        elif word in (')', ']', '}'):
            if not stack or stack[-1]['delimiter'] != {')': '(', ']': '[', '}': '{'}[word]:
                raise ValueError('unbalanced source delimiters')
            stack.pop()['close'] = i
        elif word == ',' and stack:
            stack[-1]['commas'].append(i)
        if word in names:
            matches.append((i, tuple(stack)))
    if stack:
        raise ValueError('unbalanced source delimiters')
    reads = {'gSPDisplayList': 1, 'gSPBranchList': 1, 'gsSPDisplayList': 0, 'gsSPBranchList': 0}
    controls = {'if', 'for', 'while', 'switch'}
    result = []
    for i, ancestors in matches:
        row = {'name': words[i], 'line': bisect.bisect_right(lines, tokens[i][1]) + 1,
               'kind': 'unknown'}
        if i and words[i - 1] == 'Gfx' and i + 1 < len(words) and words[i + 1] == '[':
            end = groups.get(i + 1, {}).get('close', len(words))
            after = words[end + 1:end + 3]
            if after == ['=', '{']:
                row['kind'] = 'definition'
            elif after and after[0] == ';':
                row['kind'] = 'declaration'
        if row['kind'] == 'unknown':
            cursor = i + 1
            while cursor < len(words):
                if words[cursor] == '[' and cursor in groups:
                    cursor = groups[cursor]['close'] + 1
                elif words[cursor] in ('.', '->') and cursor + 1 < len(words):
                    cursor += 2
                else:
                    break
            if cursor < len(words) and words[cursor] in ('=', '+=', '-=', '*=', '/=', '%=',
                                                         '&=', '|=', '^=', '<<=', '>>=', '++', '--'):
                row['kind'] = 'writer'
                row['operation'] = words[cursor]
            elif i and words[i - 1] in ('++', '--'):
                row.update(kind='writer', operation=words[i - 1])
        if row['kind'] == 'unknown':
            calls = [g for g in ancestors if g.get('name') and g['name'] not in controls]
            if calls:
                group = calls[-1]
                boundaries = [group['open']] + group['commas'] + [group['close']]
                argument = next(n for n in range(len(boundaries) - 1)
                                if boundaries[n] < i < boundaries[n + 1])
                call = group['name']
                row.update(call=call, argument=argument)
                value = words[boundaries[argument] + 1:boundaries[argument + 1]]
                while len(value) >= 3 and value[0] == '(' and value[-1] == ')':
                    value = value[1:-1]
                if writer_arguments.get(call) == argument:
                    row['kind'] = 'writer_argument'
                elif call == 'sizeof':
                    row['kind'] = 'size_read'
                elif reads.get(call) == argument and value == [words[i]]:
                    row['kind'] = 'display_list_read'
                else:
                    row['kind'] = 'call_alias'
            elif any(g['delimiter'] == '{' and g['open'] and words[g['open'] - 1] == '='
                     for g in ancestors):
                row['kind'] = 'initializer_alias'
            elif ((i and words[i - 1] in ('==', '!=')) or
                  (i + 1 < len(words) and words[i + 1] in ('==', '!='))):
                row['kind'] = 'identity_read'
        result.append(row)
    return result


def active_source(path, build):
    flags_path = Path(build) / 'compile-flags.stamp'
    flags = shlex.split(flags_path.read_text())
    result = subprocess.run(['psp-gcc', *flags, '-E', '-fdirectives-only', str(path)],
                            capture_output=True, text=True)
    if result.returncode:
        raise ValueError('source preprocessing failed: ' + result.stderr.strip())
    output = ['\n'] * len(path.read_text().splitlines())
    target = path.resolve()
    current = None
    number = 1
    for line in result.stdout.splitlines(keepends=True):
        marker = re.match(r'^#\s+(\d+)\s+"([^"]+)"', line)
        if marker:
            number = int(marker[1])
            current = Path(marker[2]).resolve()
        else:
            if current == target and 0 < number <= len(output):
                output[number - 1] = line
            number += 1
    return ''.join(output)


def binding_observer(user):
    if user['source'] != 'src/psp/gfx/gfx_psp_dl.c' or user['role'] != 'code' or not user['owner']:
        return False
    owner = user['owner'].split('.')[0]
    return (owner == 'psp_gfx_dl_run_internal' or
            owner.startswith('psp_gfx_dl_native_') and owner.endswith('_dispatch'))


def mutation_audit(build, assets, users):
    root = Path(__file__).resolve().parents[3]
    names = {a['name'] for a in assets}
    writers = {'memcpy': 0, 'memmove': 0, 'memset': 0, 'bcopy': 1, 'bzero': 0}
    gbi_path = root / 'include/PR/gbi.h'
    gbi = gbi_path.read_text()
    for name in re.findall(r'#\s*define\s+(\w+)\s*\(\s*pkt\s*[,)]', gbi):
        writers[name] = 0
    paths = {root / obj['source'] for obj in users['objects'] if (root / obj['source']).is_file()}
    paths.update(p for p in (root / 'include').rglob('*.h') if p.name != 'mods.h')
    pending = list(sorted(paths))
    records = []
    errors = []
    files = []
    expressions = re.compile(r'\b(?:' + '|'.join(sorted(names)) + r')\b')
    while pending:
        path = pending.pop()
        text = path.read_text()
        source = str(path.relative_to(root))
        files.append({'source': source, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
        for include in re.findall(r'^\s*#\s*include\s+"([^"\n]+\.inc\.c)"', text, re.M):
            options = (root / include, path.parent / include)
            child = next((p.resolve() for p in options if p.is_file()), None)
            if child and child.is_relative_to(root) and child not in paths:
                paths.add(child)
                pending.append(child)
        if not expressions.search(text):
            continue
        try:
            try:
                uses = source_uses(text, names, writers)
            except ValueError:
                uses = source_uses(active_source(path, build), names, writers)
                files[-1]['active_branches'] = True
            records.extend(dict(row, source=source) for row in uses)
        except ValueError as error:
            errors.append({'source': source, 'reason': str(error),
                           'names': sorted(set(expressions.findall(text)))})
    by_name = collections.defaultdict(list)
    address_users = collections.defaultdict(list)
    for row in records:
        by_name[row['name']].append(row)
    for row in users['users']:
        address_users[row['asset']].append(row)
    errors_by_source = {e['source'] for e in errors}
    error_names = {name for e in errors for name in e['names']}
    decisions = []
    for asset in assets:
        name = asset['name']
        uses = by_name[name]
        observers = [u for u in address_users[name] if binding_observer(u)]
        addresses = [u for u in address_users[name] if not binding_observer(u)]
        writer_uses = [u for u in uses if u['kind'] in ('writer', 'writer_argument')]
        aliases = [u for u in uses if u['kind'] in ('unknown', 'call_alias', 'initializer_alias')]
        covered_sources = {u['source'] for u in uses if u['kind'] not in ('definition', 'declaration')}
        uncovered = sorted({u['source'] for u in addresses} - covered_sources)
        if writer_uses:
            status = 'writer_review'
        elif (aliases or uncovered or any(u['role'] == 'data' for u in addresses) or
              name in error_names or any(u['source'] in errors_by_source for u in addresses)):
            status = 'alias_review'
        elif not addresses:
            status = 'no_named_users'
        else:
            status = 'direct_reads_only'
        decisions.append({'name': name, 'supported': asset['status'] == 'supported',
                          'enabled': asset['enabled'], 'status': status,
                          'source_uses': len(uses), 'writer_uses': len(writer_uses),
                          'alias_uses': len(aliases), 'uncovered_sources': uncovered,
                          'binding_observers': len(observers)})
    return {'scope': 'direct source uses with indirect aliases and macro bodies requiring review',
            'gbi_sha256': hashlib.sha256(gbi_path.read_bytes()).hexdigest(),
            'compile_flags_sha256': hashlib.sha256((Path(build) / 'compile-flags.stamp').read_bytes()).hexdigest(),
            'summary': dict(sorted(collections.Counter(d['status'] for d in decisions).items())),
            'files': sorted(files, key=lambda f: f['source']), 'uses': records,
            'decisions': decisions, 'errors': errors}


def selection_policy(assets, audit):
    decisions = {d['name']: d for d in audit['decisions']}
    rows = []
    for asset in assets:
        decision = decisions[asset['name']]
        if asset['enabled']:
            if decision['writer_uses']:
                raise ValueError('writer evidence conflicts with an enabled asset: ' + asset['name'])
            status = 'accepted'
        elif asset['status'] != 'supported':
            status = 'unsupported'
        elif decision['status'] == 'writer_review':
            status = 'writer_review'
        elif decision['status'] == 'direct_reads_only':
            status = 'validation_candidate'
        else:
            status = decision['status']
        rows.append({'name': asset['name'], 'source': asset['source'], 'status': status,
                     'commands': asset.get('commands'), 'audit_status': decision['status']})
    return {'version': 1, 'scope': 'selection proposal only with no automatic emission',
            'candidate_requirements': ['review indirect write paths', 'differential validation',
                                       'emitted text budget', 'PSP acceptance'],
            'summary': dict(sorted(collections.Counter(r['status'] for r in rows).items())),
            'assets': rows}


def inventory(build):
    root = Path(__file__).resolve().parents[3]
    objects = []
    assets = []
    names = set()
    snapshots = {}
    for source in sorted((root / 'src/assets').glob('*/*.c')):
        if source.stem != source.parent.name:
            continue
        relative = source.relative_to(root)
        header = 'assets/' + source.stem + '.h'
        header_path = root / 'include' / header
        path = Path(build) / relative.with_suffix('.o')
        data, sections, symbols, symbol_section = read_symbols(path)
        interface = header_path.read_text()
        snapshots[str(relative)] = (data, sections, symbols, symbol_section, interface)
        exported = re.findall(r'extern Gfx (\w+)\[[^\]]*\];', interface)
        if len(set(exported)) != len(exported) or names.intersection(exported):
            raise ValueError('duplicate display list interface in ' + str(relative))
        names.update(exported)
        objects.append({'source': str(relative), 'header': header, 'display_lists': len(exported),
                        'object_sha256': hashlib.sha256(data).hexdigest(),
                        'header_sha256': hashlib.sha256(header_path.read_bytes()).hexdigest()})
        for name in exported:
            row = {'name': name, 'source': str(relative), 'enabled': name in ASSETS,
                   'status': 'blocked'}
            stage = 'decode'
            try:
                words, relocations, _ = decode_asset(data, sections, symbols, symbol_section, name)
                row['commands'] = len(words)
                row['ops'] = dict(sorted(collections.Counter(f'{w0 >> 24:02x}' for w0, _ in words).items()))
                stage = 'commands'
                _, events = compile_asset(words, relocations)
                stage = 'interface'
                asset_interface(header, name, words, relocations)
                row.update(status='supported', vtx=sum(e[0] == 'load' for e in events),
                           loaded=sum(e[3] for e in events if e[0] == 'load'),
                           triangles=sum(2 if e[0] == 'tri2' else 1 for e in events
                                         if e[0] in ('tri1', 'tri2')),
                           references=sorted({r[0] for r in relocations.values()}))
            except ValueError as error:
                row.update(stage=stage, reason=str(error))
            assets.append(row)
    if set(ASSETS) - names or any(row['enabled'] and row['status'] != 'supported' for row in assets):
        raise ValueError('enabled asset failed corpus inventory')
    blockers = collections.Counter(row['stage'] + ': ' + row['reason'] for row in assets
                                   if row['status'] == 'blocked')
    supported = sum(row['status'] == 'supported' for row in assets)
    users = command_users(build, names)
    audit = mutation_audit(build, assets, users)
    return {'version': 1, 'build_dir': str(build),
            'scope': 'exported US display lists',
            'support_contract': 'current leaf compiler only with command immutability unverified',
            'summary': {'objects': len(objects), 'display_lists': len(assets),
                        'supported': supported, 'blocked': len(assets) - supported,
                        'enabled': len(ASSETS), 'blockers': dict(sorted(blockers.items()))},
            'objects': objects, 'assets': assets, 'reference_index': reference_index(snapshots),
            'command_users': users, 'mutation_audit': audit,
            'selection_policy': selection_policy(assets, audit)}


def write_output(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, newline='\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--asset')
    parser.add_argument('--coverage-policy')
    parser.add_argument('--candidate', action='append', help='compile only these qualified additions without A/B controls')
    parser.add_argument('--sources', action='store_true')
    parser.add_argument('--inventory', action='store_true')
    parser.add_argument('--build-dir')
    parser.add_argument('--verify-elf')
    parser.add_argument('--report')
    parser.add_argument('paths', nargs='*')
    args = parser.parse_args()
    if args.candidate and not args.coverage_policy:
        parser.error('--candidate requires --coverage-policy')
    if args.coverage_policy:
        if args.inventory:
            parser.error('coverage selection consumes saved evidence without a new audit')
        select_coverage(args.coverage_policy, args.candidate)
    if args.inventory and args.build_dir and len(args.paths) == 1 and not (args.asset or args.sources or args.verify_elf or args.report):
        result = inventory(args.build_dir)
        write_output(args.paths[0], json.dumps(result, indent=2) + '\n')
        print(json.dumps(result['summary'], sort_keys=True))
    elif args.inventory:
        parser.error('choose --inventory --build-dir BUILD OUTPUT')
    elif args.sources and not (args.paths or args.asset or args.build_dir or args.verify_elf or args.report):
        print(' '.join(g['source'] for g in MANIFEST['objects']))
    elif args.verify_elf and args.build_dir and not (args.paths or args.asset or args.sources or args.report):
        verify_elf(args.build_dir, args.verify_elf)
    elif args.asset and len(args.paths) == 2 and not (args.build_dir or args.sources or args.report or args.verify_elf):
        write_output(args.paths[1], generate(args.paths[0], args.asset))
    elif args.build_dir and len(args.paths) == 1 and not (args.asset or args.sources or args.verify_elf):
        result, report = generate_all(args.build_dir)
        write_output(args.paths[0], result)
        if args.report:
            write_output(args.report, json.dumps(report, indent=2) + '\n')
    else:
        parser.error('choose --sources, --asset OBJECT OUTPUT, or [--inventory] --build-dir BUILD OUTPUT')


if __name__ == '__main__':
    main()
