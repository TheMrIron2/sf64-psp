"""Fit renderer code into production reservations with automatic slot growth"""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--base', required=True)
parser.add_argument('--object', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
plan = json.loads(Path(args.base).read_text())
output = subprocess.check_output(['psp-nm', '-S', '--defined-only', args.object], text=True)
sizes = {p[3]: int(p[1], 16) for line in output.splitlines() if len(p := line.split()) == 4}
changes = []
for index, chunk in enumerate(plan['chunks']):
    previous_end = (plan['chunks'][index - 1]['address'] + plan['chunks'][index - 1]['slot']) if index else 0
    chunk['address'] = max(chunk['address'], previous_end)
    if chunk['owner'] != 'src/psp/gfx/gfx_psp_dl.o':
        continue
    for function in chunk['functions']:
        actual = sizes.get(function['name'])
        if actual == function['size']:
            continue
        if actual is None:
            raise ValueError('missing pinned renderer function: ' + function['name'])
        extra = max(0, ((function['offset'] + actual + 63) & ~63) - chunk['slot'])
        if extra:
            shift = min(extra, chunk['address'] - previous_end)
            chunk['address'] -= shift
            chunk['slot'] += extra
            remaining = extra - shift
            for donor_index in range(index - 1, -1, -1):
                if not remaining:
                    break
                donor = plan['chunks'][donor_index]
                used = max(f['offset'] + f['size'] for f in donor['functions'])
                take = min(remaining, donor['slot'] - ((used + 63) & ~63))
                donor['slot'] -= take
                for moved in plan['chunks'][donor_index + 1:index + 1]:
                    moved['address'] -= take
                remaining -= take
        changes.append((function['name'], function['size'], actual, extra))
        function['size'] = actual
plan['pinned_end'] = plan['chunks'][-1]['address'] + plan['chunks'][-1]['slot']
Path(args.output).write_text(json.dumps(plan, indent=2) + '\n')
print('Renderer reserved layout:', changes)
