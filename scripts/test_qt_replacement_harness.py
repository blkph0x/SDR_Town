"""Negative tests for DEC-0179's source-replacement harness, without compiling Qt."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from qt_sources import CONFIG_FILES
from test_qt_replacement import (base_options, check_base_cache, extract_module,
                                 feature_differences, replacement_files, run, runtime_environment, test)


class ReplacementTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.install = self.root / 'install'

    def write(self, relative, data=b'fixture'):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_all_packaged_qt_files_are_replaced(self):
        files = {'Qt6Core.dll': None, 'platforms/qwindows.dll': None, 'rtlsdr.dll': None,
                 'package-inventory.json': None}
        self.write('install/bin/Qt6Core.dll')
        self.write('install/plugins/platforms/qwindows.dll')
        self.assertEqual(set(replacement_files(files, self.install, True)),
                         {'Qt6Core.dll', 'platforms/qwindows.dll'})

    def test_missing_plugin_fails_no_sdk_fallback(self):
        self.write('install/bin/Qt6Core.dll')
        with self.assertRaises((ValueError, OSError)):
            replacement_files({'Qt6Core.dll': None, 'platforms/qwindows.dll': None}, self.install, True)

    def test_empty_selection_fails(self):
        with self.assertRaises(ValueError):
            replacement_files({'rtlsdr.dll': None}, self.install, True)

    def test_unknown_runtime_fails(self):
        with self.assertRaises(ValueError):
            replacement_files({'mystery.dll': None}, self.install, True)

    def test_svg_only_keeps_narrow_scope(self):
        names = ('Qt6Svg.dll', 'imageformats/qsvg.dll', 'iconengines/qsvgicon.dll')
        for name in names:
            self.write('install/' + ('plugins/' if '/' in name else 'bin/') + name)
        files = {name: None for name in (*names, 'Qt6Core.dll')}
        self.assertEqual(set(replacement_files(files, self.install, False)), set(names))

    def archive(self, name, symlink=False):
        out = io.BytesIO()
        with tarfile.open(fileobj=out, mode='w:xz') as archive:
            info = tarfile.TarInfo(name)
            if symlink:
                info.type, info.linkname = tarfile.SYMTYPE, '../outside'
                archive.addfile(info)
            else:
                info.size = 3
                archive.addfile(info, io.BytesIO(b'abc'))
        return out.getvalue()

    def test_extract_regular(self):
        extract_module(self.archive('module/sub/file'), self.root / 'source')
        self.assertEqual((self.root / 'source/sub/file').read_bytes(), b'abc')

    def test_extract_escape_fails(self):
        with self.assertRaises(ValueError):
            extract_module(self.archive('module/../outside'), self.root / 'source')
        self.assertFalse((self.root / 'outside').exists())

    def test_extract_link_fails(self):
        with self.assertRaises(ValueError):
            extract_module(self.archive('module/link', True), self.root / 'source')

    def test_feature_diff_includes_absent_and_changed(self):
        for name in CONFIG_FILES:
            self.write('sdk/' + name, b'#define QT_FEATURE_shared 1\n')
            self.write('rebuilt/' + name, b'#define QT_FEATURE_shared -1\n#define QT_FEATURE_added 1\n')
        differences = feature_differences(self.root / 'sdk', self.root / 'rebuilt')
        self.assertEqual(differences[CONFIG_FILES[0]]['QT_FEATURE_shared']['rebuilt'], '-1')
        self.assertEqual(differences[CONFIG_FILES[0]]['QT_FEATURE_added']['sdk'], 'absent')

    def test_failed_run_cannot_leave_previous_success(self):
        stage, output = self.root / 'stage', self.root / 'out'
        stage.mkdir()
        self.write('out/result.json', b'{"status":"pass","fullQtRebuildVerified":true}')
        with patch('test_qt_replacement.perform_test', side_effect=ValueError('bad kit')):
            with self.assertRaisesRegex(ValueError, 'bad kit'):
                test({'qtVersion': '6.7.3'}, stage, output, self.root, full=True)
        result = json.loads((output / 'result.json').read_bytes())
        self.assertEqual(result['status'], 'failed')
        self.assertFalse(result['fullQtRebuildVerified'])

    def test_output_inside_stage_rejected_before_writes(self):
        stage = self.root / 'stage'
        stage.mkdir()
        original = self.write('stage/result.json', b'original')
        with self.assertRaises(ValueError):
            test({'qtVersion': '6.7.3'}, stage, stage, self.root)
        self.assertEqual(original.read_bytes(), b'original')

    def test_command_log_and_exit_failure(self):
        log = self.root / 'command.log'
        with self.assertRaises(subprocess.CalledProcessError):
            run([sys.executable, '-c', 'print("failure-fixture"); raise SystemExit(2)'], log)
        self.assertIn('failure-fixture', log.read_text())
        self.assertIn('timeoutSeconds', log.read_text())

    def test_system_dependency_is_rejected(self):
        cache = '// Fixture comment\n\n'
        for option in base_options():
            name, value = option[2:].split('=', 1)
            computed = 'QT_' + name if name.startswith('FEATURE_') else 'QT_FEATURE_shared'
            cache += f'{name}:BOOL={value}\n// Another comment\n{computed}:INTERNAL={value}\n\n'
        path = self.write('CMakeCache.txt', cache.encode())
        check_base_cache(path)
        path.write_text(cache.replace('FEATURE_system_zlib:BOOL=OFF', 'FEATURE_system_zlib:BOOL=ON'))
        with self.assertRaisesRegex(ValueError, 'system_zlib'):
            check_base_cache(path)
        path.write_text(cache.replace('QT_FEATURE_system_zlib:INTERNAL=OFF',
                                      'QT_FEATURE_system_zlib:INTERNAL=ON'))
        with self.assertRaisesRegex(ValueError, 'computed configuration'):
            check_base_cache(path)

    def test_runtime_path_excludes_developer_tools(self):
        with patch.dict(os.environ, {'PATH': 'unrelated-sdk', 'QT_PLUGIN_PATH': 'unrelated-plugins'}):
            env = runtime_environment(self.root)
        self.assertNotIn('unrelated-sdk', env['PATH'])
        self.assertNotIn('QT_PLUGIN_PATH', env)
        self.assertEqual(env['QT_QPA_PLATFORM'], 'windows')

    def test_explicit_openssl_headers_cannot_enable_linked_runtime(self):
        root = self.root / 'openssl'
        options = base_options(root)
        self.assertIn('-DFEATURE_openssl=ON', options)
        self.assertIn('-DFEATURE_openssl_linked=OFF', options)
        self.assertIn(f'-DOPENSSL_INCLUDE_DIR={root}/include', options)


if __name__ == '__main__':
    unittest.main()
