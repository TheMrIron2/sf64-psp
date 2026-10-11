import argparse
import json
import re
import subprocess
from pathlib import Path


def input_sections(map_path):
    rows = []
    active = False
    pending = None
    for line in map_path.read_text().splitlines():
        if line.startswith('.text '):
            active = True
            continue
        if active and line.startswith('.init '):
            break
        if not active:
            continue
        match = re.match(r'^ (\.\S+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S.*)$', line)
        if match:
            section, address, size, owner = match.groups()
            pending = None
        elif re.match(r'^ (\.\S+)\s*$', line):
            pending = line.strip()
            continue
        elif pending and (match := re.match(r'^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S.*)$', line)):
            address, size, owner = match.groups()
            section = pending
            pending = None
        else:
            continue
        size = int(size, 16)
        if size:
            if '.o' not in owner:
                raise ValueError(f'unexpected text input owner {owner}')
            rows.append({
                'section': section,
                'address': int(address, 16),
                'size': size,
                'owner': owner,
            })
    if len(rows) < 3000:
        raise ValueError(f'expected full text map, found {len(rows)} input sections')
    return rows


def section_selectors(map_path, output_section):
    selectors = []
    active = False
    pending = None
    for line in map_path.read_text().splitlines():
        if line.startswith(output_section + ' '):
            active = True
            continue
        if active and line and not line.startswith(' '):
            break
        if not active:
            continue
        match = re.match(r'^ (\.\S+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S.*)$', line)
        if match:
            section, _, size, owner = match.groups()
            pending = None
        elif re.match(r'^ (\.\S+)\s*$', line):
            pending = line.strip()
            continue
        elif pending and (match := re.match(r'^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S.*)$', line)):
            _, size, owner = match.groups()
            section = pending
            pending = None
        else:
            continue
        if '.o' not in owner:
            raise ValueError(f'unexpected {output_section} input owner {owner}')
        if '.ltrans' in owner:
            owner = '*ltrans*.o'
        else:
            owner = re.sub(r'^(.*)\(([^()]+)\)$', r'\1:\2', owner)
        selectors.append(f'"{owner}"({section})')
    return selectors


def make_linker_script(template, rows, relocations, text_size, map_path):
    targets = []
    for relocation in relocations:
        matches = [row for row in rows
                   if row['section'] == relocation['section']
                   and row['owner'].endswith(
                       f"{relocation['archive']}({relocation['member']})")]
        if len(matches) != 1:
            raise ValueError(f"expected one input for {relocation['archive']}({relocation['member']})")
        row = matches[0]
        if row['size'] != relocation['size']:
            raise ValueError(f"{row['owner']} size changed from {relocation['size']} to {row['size']}")
        row['address'] = relocation['address']
        targets.append(row)

    rows.sort(key=lambda row: row['address'])
    end = 0
    body = ['    _ftext = .;']
    for row in rows:
        address = row['address']
        size = row['size']
        if address < end:
            raise ValueError(f"overlapping text input {row['owner']} at {address:#x}")
        owner = row['owner']
        if '.ltrans' in owner:
            owner = '*ltrans*.o'
        else:
            owner = re.sub(r'^(.*)\(([^()]+)\)$', r'\1:\2', owner)
        body.extend([
            f'    . = 0x{address:x};',
            f'    "{owner}"({row["section"]})',
            f'    ASSERT(. == 0x{address + size:x}, "input extent changed");',
        ])
        end = address + size
    body.extend([
        f'    . = 0x{text_size:x};',
        '    *(.text .stub .text.* .gnu.linkonce.t.*)',
        '    KEEP (*(.text.*personality*))',
        '    *(.gnu.warning)',
        '    *(.mips16.fn.*) *(.mips16.call.*)',
        f'    ASSERT(. == 0x{text_size:x}, "unexpected text input");',
    ])
    start = template.index('  .text :')
    finish = template.index('  .init ', start)
    replacement = '  .text :\n  {\n' + '\n'.join(body) + '\n  } =0\n'
    result = template[:start] + replacement + template[finish:]
    old_eh_frame = 'KEEP (*(.eh_frame))'
    new_eh_frame = 'KEEP (*(EXCLUDE_FILE (*crtend.o) .eh_frame)) KEEP (*crtend.o(.eh_frame))'
    if old_eh_frame not in result:
        raise ValueError('could not find unwind terminator selector')
    result = result.replace(old_eh_frame, new_eh_frame)
    patterns = {
        '.rodata': '*(.rodata .rodata.* .gnu.linkonce.r.*)',
        '.data': '*(.data .data.* .gnu.linkonce.d.*)',
        '.sdata': '*(.sdata .sdata.* .gnu.linkonce.s.*)',
        '.sbss': '*(.sbss .sbss.* .gnu.linkonce.sb.*)',
        '.bss': '*(.bss .bss.* .gnu.linkonce.b.*)',
    }
    for output_section, pattern in patterns.items():
        selectors = section_selectors(map_path, output_section)
        if not selectors:
            raise ValueError(f'no input selectors found for {output_section}')
        if pattern not in result:
            raise ValueError(f'could not find default selector for {output_section}')
        result = result.replace(pattern, '\n    '.join(selectors) + '\n    ' + pattern)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--map', required=True, type=Path)
    parser.add_argument('--elf', required=True, type=Path)
    parser.add_argument('--template', required=True, type=Path)
    parser.add_argument('--relocations', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    rows = input_sections(args.map)
    config = json.loads(args.relocations.read_text())
    size_output = subprocess.check_output(['psp-size', '-A', str(args.elf)], text=True)
    match = re.search(r'^\.text\s+(\d+)\s+', size_output, re.MULTILINE)
    if not match:
        raise ValueError('could not read linked text size')
    text_size = int(match.group(1))
    script = make_linker_script(
        args.template.read_text(), rows, config['inputs'], text_size, args.map)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(script)
    print(f'Generated fixed text placement for {len(rows)} input sections and {len(config["inputs"])} relocations')


if __name__ == '__main__':
    main()
