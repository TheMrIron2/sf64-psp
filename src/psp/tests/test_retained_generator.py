import importlib.util
import contextlib
import io
import json
import runpy
import struct
from pathlib import Path
import os
import tempfile
import unittest
from unittest.mock import patch
import hashlib

spec = importlib.util.spec_from_file_location('compiler', Path(__file__).resolve().parents[1]/'gfx/native_asset_compile.py')
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)


class RetainedGeneratorTest(unittest.TestCase):
    def mesh(self, loads=3, count=16):
        words, relocations = [], {}
        for i in range(loads):
            relocations[len(words)] = ('vertices', 0, count*16, 1)
            words.extend([((4<<24)|(count<<10)|(count*16-1), 0), (0xbf000000, 0x00000204)])
        words.append((0xb8000000, 0))
        return words, relocations

    def test_slot_overwrite_and_material_span_merging(self):
        words, relocations = self.mesh()
        cost = compiler.retained_cost(words, relocations)
        self.assertEqual((cost['loaded'],cost['final'],cost['avoided']), (48,16,32))
        self.assertEqual((cost['spans'],cost['load_spans']), (1,3))
        words, relocations = self.mesh(loads=1)
        cost = compiler.retained_cost(words,relocations)
        self.assertEqual((cost['avoided'],cost['spans']), (0,1))

    def test_inherited_vertices_and_bad_extents_rejected(self):
        with self.assertRaises(ValueError):
            compiler.retained_cost([(0xbf000000,0x00000204),(0xb8000000,0)], {})
        words, relocations = self.mesh()
        relocations[0] = ('vertices',0,16,1)
        with self.assertRaises(ValueError):
            compiler.retained_cost(words,relocations)

    def test_capacity_and_wide_version_span(self):
        words, relocations = self.mesh(loads=257)
        with self.assertRaises(ValueError):
            compiler.retained_cost(words,relocations)
        words, relocations = self.mesh(loads=1,count=32)
        words.pop()
        for i in range(4):
            relocations[len(words)] = ('vertices',0,31*16,1)
            words.append(((4<<24)|(31<<10)|(31*16-1),0))
        words.extend([(0xbf000000,0x003e0200),(0xb8000000,0)])
        cost = compiler.retained_cost(words,relocations)
        self.assertGreater(cost['loaded'],64)
        self.assertEqual(cost['streamed_vertices'],6)

    def test_frequency_ranking_uses_gameplay_peak_and_aggregates_sources(self):
        rows=[dict(name='large',cost=dict(avoided=500)),dict(name='repeated',cost=dict(avoided=65))]
        captures=[dict(scene='Title',capture='title',asset_rows=[dict(name='large',calls_per_frame=100)]),
                  dict(scene='moving',capture='game',asset_rows=[dict(name='large',calls_per_frame=1),dict(name='repeated',calls_per_frame=6),dict(name='repeated',calls_per_frame=4)]),
                  dict(scene='paused',capture='pause',asset_rows=[dict(name='repeated',calls_per_frame=2)])]
        ranked=compiler.rank_retained_frequency(rows,captures)
        self.assertEqual(ranked[0]['name'],'repeated')
        self.assertEqual(ranked[0]['avoided_per_frame'],650)
        self.assertEqual(ranked[1]['avoided_per_frame'],500)

    def test_generation_rejects_missing_evidence_and_changed_vertices(self):
        row=dict(name='mesh',source='src/assets/ast_fake/ast_fake.c',qualification='direct_reads_only',source_sha256={},vertex_sha256={'vertices':'original'})
        with patch.object(compiler,'ACCEPTED_MANIFEST',{'retained':dict(version=1,assets=[row])}):
            row['qualification']='accepted_commands'
            with self.assertRaisesRegex(ValueError,'outside accepted'):
                compiler.qualify_retained('unused', row)
            row['qualification']='direct_reads_only'
            with self.assertRaisesRegex(ValueError,'incomplete'):
                compiler.qualify_retained('unused', row)
            row['source_sha256']={path:hashlib.sha256(b'source').hexdigest() for path in ['src/assets/ast_fake/ast_fake.c','include/assets/ast_fake.h','include/PR/gbi.h']}
            with patch.object(compiler.Path,'read_bytes',return_value=b'source'), patch.object(compiler,'retained_payloads',return_value=({}, {'vertices':'changed'})):
                with self.assertRaisesRegex(ValueError,'payload changed'):
                    compiler.qualify_retained('unused', row)

    def test_reviewed_table_rejects_new_consumers(self):
        paths=['src/assets/ast_fake/ast_fake.c','include/assets/ast_fake.h','include/PR/gbi.h','src/engine/render.c']
        row=dict(name='mesh',source=paths[0],qualification='reviewed_render_table',
                 alias_review=dict(symbol='renderTable',consumers=['src/engine/render.c']),
                 source_sha256={path:hashlib.sha256(b'source').hexdigest() for path in paths},vertex_sha256={})
        with patch.object(compiler,'ACCEPTED_MANIFEST',{'retained':dict(version=1,assets=[row])}), patch.object(compiler.Path,'read_bytes',return_value=b'source'), patch.object(compiler,'retained_runtime_users',return_value={'renderTable':['src/engine/unreviewed.c']}):
            with self.assertRaisesRegex(ValueError,'consumers changed'):
                compiler.qualify_retained('unused', row)

    def row(self, name='mesh'):
        paths = ['src/assets/ast_fake/ast_fake.c', 'include/assets/ast_fake.h', 'include/PR/gbi.h']
        return dict(name=name, source=paths[0], qualification='direct_reads_only',
                    source_sha256={path: hashlib.sha256(b'source').hexdigest() for path in paths},
                    vertex_sha256={name + '_vertices': 'original'})

    def generate(self, rows, users=None, setup_error=None, payload_error=None, shared=False, read_error=None):
        cost = dict(commands=7, packet_bytes=256, written_slots=[7, 0], final=3)
        symbols = [(row['name'], i*8, 8, 1) for i, row in enumerate(rows)]
        def payloads(build, row):
            if payload_error:
                raise ValueError(payload_error)
            hashes = {row['name'] + '_vertices': 'original'}
            if shared:
                hashes['mesh_vertices'] = 'original'
            return cost, hashes
        warnings = io.StringIO()
        with tempfile.TemporaryDirectory() as directory, contextlib.ExitStack() as stack:
            stack.enter_context(patch.object(compiler, 'ACCEPTED_MANIFEST',
                                             {'retained': dict(version=1, assets=rows)}))
            stack.enter_context(patch.object(compiler.Path, 'read_bytes', return_value=b'source', side_effect=read_error))
            stack.enter_context(patch.object(compiler, 'retained_payloads', side_effect=payloads))
            stack.enter_context(patch.object(compiler, 'retained_runtime_users', return_value=users or {}))
            stack.enter_context(patch.object(compiler, 'read_symbols', return_value=(None, None, symbols, None)))
            stack.enter_context(patch.object(compiler, 'asset_object_order', return_value={rows[0]['source']: 0}))
            stack.enter_context(patch.object(compiler, 'retained_setup_rows', return_value=4,
                                             side_effect=ValueError(setup_error) if setup_error else None))
            stack.enter_context(contextlib.redirect_stderr(warnings))
            stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
            compiler.generate_retained(directory)
            output = Path(directory)
            return (json.loads((output/'retained_assets.json').read_text()),
                    (output/'retained_assets.h').read_text(),
                    (output/'retained_assets.inc.c').read_text(), warnings.getvalue())

    def test_stale_caller_only_disables_affected_leaf(self):
        stale, valid = self.row(), self.row('valid')
        stale['source_sha256']['src/engine/fox_enmy.c'] = 'old'
        rows, header, source, warnings = self.generate([stale, valid])
        self.assertEqual([row['name'] for row in rows], ['valid'])
        self.assertIn('PSP_RETAINED_PACKET_BYTES 256', header)
        self.assertNotIn('extern Gfx mesh[]', source)
        self.assertIn('normal rendering for 1 retained leaves', warnings)
        self.assertIn('stale retained source evidence for src/engine/fox_enmy.c', warnings)

    def test_empty_selection_has_no_asset_references_or_zero_sized_arrays(self):
        for reason in ('stale', 'missing', 'payload', 'runtime'):
            with self.subTest(reason=reason):
                row = self.row()
                users, error = None, None
                if reason == 'stale':
                    row['source_sha256']['include/PR/gbi.h'] = 'old'
                elif reason == 'missing':
                    row['source_sha256'] = {}
                elif reason == 'payload':
                    error = 'retained vertex payload changed'
                else:
                    users = {'mesh_vertices': ['src/engine/new.c']}
                rows, header, source, warnings = self.generate([row], users, payload_error=error)
                self.assertEqual(rows, [])
                self.assertIn('PSP_RETAINED_CACHE_SLOTS 1', header)
                self.assertIn('PSP_RETAINED_PACKET_BYTES 0', header)
                self.assertIn('{ NULL, 0 }', source)
                self.assertNotIn('extern Gfx', source)
                self.assertIn('normal rendering', warnings)

    def test_new_vertex_user_disables_every_shared_leaf(self):
        first, second = self.row(), self.row('second')
        second['vertex_sha256']['mesh_vertices'] = 'original'
        rows, _, _, _ = self.generate([first, second], {'mesh_vertices': ['src/engine/new.c']}, shared=True)
        self.assertEqual(rows, [])

    def test_removed_evidence_file_uses_normal_rendering(self):
        rows, _, _, warnings = self.generate([self.row()], read_error=FileNotFoundError('removed source'))
        self.assertEqual(rows, [])
        self.assertIn('removed source', warnings)

    def test_changed_setup_contract_disables_only_setup_shortcut(self):
        rows, header, _, warnings = self.generate([self.row()], setup_error='setup contract source changed')
        self.assertEqual(len(rows), 1)
        self.assertIn('PSP_RETAINED_SETUP_ROWS 0', header)
        self.assertIn('preserve vertex outputs', warnings)

    def test_valid_evidence_preserves_registry_without_warning(self):
        rows, header, source, warnings = self.generate([self.row()])
        self.assertEqual([row['name'] for row in rows], ['mesh'])
        self.assertIn('PSP_RETAINED_SETUP_ROWS 4', header)
        self.assertIn('{ mesh, 7 }', source)
        self.assertEqual(warnings, '')

    def test_setup_table_checks_current_consumers_and_vertex_effects(self):
        consumer = 'src/engine/fox_rcp.c'
        contract = dict(setup_users=[consumer],
                        setup_source_sha256={consumer: hashlib.sha256(b'source').hexdigest()})
        words = [(0xe7000000, 0)] * 8 + [(0xb8000000, 0)]
        for failure in (None, 'consumer', 'source', 'missing', 'vertex', 'flow'):
            with self.subTest(failure=failure):
                current = list(words)
                users = {'gRcpSetupDLs': [consumer]}
                source = b'source'
                symbols = [('gRcpSetupDLs', 0, 72, 0)]
                if failure == 'consumer':
                    users['gRcpSetupDLs'].append('src/engine/new.c')
                elif failure == 'source':
                    source = b'changed'
                elif failure == 'missing':
                    symbols = []
                elif failure == 'vertex':
                    current[0] = (0x04000000, 0)
                elif failure == 'flow':
                    current[0] = (0x06000000, 0)
                data = b''.join(struct.pack('<II', *word) for word in current)
                sections = [(0, 0, 0, 0, 0, len(data))]
                with patch.object(compiler, 'retained_runtime_users', return_value=users), \
                     patch.object(compiler.Path, 'read_bytes', return_value=source), \
                     patch.object(compiler, 'read_symbols', return_value=(data, sections, symbols, None)):
                    if failure:
                        with self.assertRaises(ValueError):
                            compiler.retained_setup_rows('unused', contract)
                    else:
                        self.assertEqual(compiler.retained_setup_rows('unused', contract), 1)

    def test_runtime_vertex_use_blocks_but_comments_and_strings_do_not(self):
        previous=Path.cwd()
        try:
            with tempfile.TemporaryDirectory() as directory:
                os.chdir(directory)
                Path('src/engine').mkdir(parents=True)
                Path('src/engine/test.c').write_text('/* vertex */ char* name="vertex";\n')
                self.assertFalse(compiler.retained_runtime_users({'vertex'}))
                Path('src/engine/test.c').write_text('vertex[0].v.ob[1]=5;\n')
                self.assertIn('vertex',compiler.retained_runtime_users({'vertex'}))
        finally:
            os.chdir(previous)


class RetainedLayoutTest(unittest.TestCase):
    def generate(self, donor_size, actual, last_address=256):
        def chunk(name, address, slot, size, owner):
            return dict(section='.text.' + name, address=address, slot=slot, owner=owner,
                        functions=[dict(name=name, size=size, offset=0)])
        plan = dict(pinned_end=last_address + 64, chunks=[
            chunk('donor', 0, 192, donor_size, 'other.o'),
            chunk('renderer', 192, 64, 64, 'src/psp/gfx/gfx_psp_dl.o'),
            chunk('following', last_address, 64, 64, 'other.o')])
        script = Path(__file__).resolve().parents[1]/'gfx/aquas_layout.py'
        with tempfile.TemporaryDirectory() as directory:
            base, output = Path(directory)/'base.json', Path(directory)/'output.json'
            base.write_text(json.dumps(plan))
            symbols = f'00000000 {actual:08x} t renderer' if actual else ''
            with patch('sys.argv', [str(script), '--base', str(base), '--object', 'fixture.o', '--output', str(output)]), \
                 patch('subprocess.check_output', return_value=symbols), contextlib.redirect_stdout(io.StringIO()):
                runpy.run_path(str(script), run_name='__main__')
            result = json.loads(output.read_text())
        end = 0
        for row in result['chunks']:
            self.assertGreaterEqual(row['address'], end)
            self.assertEqual(row['address'] % 64, 0)
            self.assertEqual(row['slot'] % 64, 0)
            self.assertTrue(all(f['offset'] + f['size'] <= row['slot'] for f in row['functions']))
            end = row['address'] + row['slot']
        self.assertEqual(end, result['pinned_end'])
        return result

    def test_growth_reuses_padding_before_moving_following_pins(self):
        plan = self.generate(64, 128)
        self.assertEqual(plan['chunks'][1]['address'], 128)
        self.assertEqual(plan['chunks'][2]['address'], 256)

    def test_growth_without_padding_moves_following_slots(self):
        plan = self.generate(192, 128)
        self.assertEqual(plan['chunks'][2]['address'], 320)

    def test_existing_following_gap_absorbs_growth(self):
        plan = self.generate(192, 128, last_address=384)
        self.assertEqual(plan['chunks'][2]['address'], 384)

    def test_missing_pinned_function_still_fails(self):
        with self.assertRaisesRegex(ValueError, 'missing pinned renderer function'):
            self.generate(64, None)


if __name__ == '__main__':
    unittest.main()
