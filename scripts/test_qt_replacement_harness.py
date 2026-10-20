"""Negative tests for DEC-0179's source-replacement harness, without compiling Qt."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
import zipfile
from unittest.mock import patch

import test_qt_replacement as replacement
from qt_sources import CONFIG_FILES
from test_qt_replacement import (base_options, check_base_cache, extract_module,
                                 feature_differences, replacement_files, run, runtime_environment, test)
from test_workspace_gui import CASES, selected_cases


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

    def test_full_gui_suite_is_default(self):
        self.assertEqual(selected_cases([]), CASES)
        self.assertEqual(len(selected_cases([])), 4)
        self.assertEqual(selected_cases(['listening']), [CASES[0]])

    def test_headless_flag_cannot_claim_narrow_rebuild(self):
        stage, output = self.root / 'stage', self.root / 'out'
        stage.mkdir()
        with self.assertRaisesRegex(ValueError, 'requires the full Qt source build'):
            test({'qtVersion': '6.7.3'}, stage, output, self.root, headless_layout=True)
        self.assertEqual(json.loads((output / 'result.json').read_bytes())['status'], 'failed')


class FailureEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.runtime = self.root / 'qt-replacement-fixture'
        self.portable = self.runtime / 'portable'
        self.output = self.root / 'evidence'
        self.output.mkdir()
        self.log = self.root / 'private-temp/sdr_town_launch.log'
        self.write(self.portable / 'SDR_Town.exe', b'application')
        self.write(self.portable / 'Qt6Core.dll', b'rebuilt-qt')
        self.write(self.portable / 'platforms/qwindows.dll', b'rebuilt-platform')
        self.write(self.portable / 'qt.conf', b'[Paths]\nPlugins=.\n')

    def write(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def archive(self, prepared):
        return replacement.archive_failure_runtime(self.runtime, self.portable, prepared, self.output)

    def test_archive_exact_prepared_bytes_not_credentials_captures_or_source_kits(self):
        for name in ('remote_diagnostics.json', 'settings.ini', 'input.iq', 'environment.txt',
                     'licenses/qt/source-materials.zip', 'SDR_Town.pdb'):
            self.write(self.portable / name, b'PRIVATE-DO-NOT-COLLECT')
        prepared = replacement.prepared_runtime(self.portable)
        self.write(self.portable / 'unprepared.dll', b'not-present-at-launch')
        result = self.archive(prepared)
        self.assertEqual(result['status'], 'saved')
        with zipfile.ZipFile(self.output / result['file']) as archive:
            self.assertEqual(set(archive.namelist()),
                             {'SDR_Town.exe', 'Qt6Core.dll', 'platforms/qwindows.dll', 'qt.conf'})
            for name in archive.namelist():
                self.assertEqual(archive.read(name), (self.portable / name).read_bytes())
        self.assertEqual(result['files'], prepared)

    def test_changed_runtime_does_not_publish_partial_archive(self):
        prepared = replacement.prepared_runtime(self.portable)
        self.write(self.portable / 'Qt6Core.dll', b'changed-qt')
        with self.assertRaisesRegex(ValueError, 'contents changed'):
            self.archive(prepared)
        self.assertFalse((self.output / 'portable.zip').exists())

    def test_resource_and_containment_bounds(self):
        prepared = replacement.prepared_runtime(self.portable)
        for setting in ('MAX_FILES', 'MAX_BYTES'):
            with self.subTest(setting=setting), patch.object(replacement, setting, 1):
                with self.assertRaises(ValueError):
                    self.archive(prepared)
        with self.assertRaisesRegex(ValueError, 'outside'):
            replacement.archive_failure_runtime(self.runtime, self.portable, prepared, self.portable)
        with self.assertRaisesRegex(ValueError, 'temporary portable'):
            replacement.archive_failure_runtime(self.root, self.portable, prepared, self.output)
        with self.assertRaises(ValueError):
            self.archive({**prepared, '../outside.dll': {'bytes': 0, 'sha256': ''}})
        self.assertFalse((self.output / 'portable.zip').exists())

    def test_reparse_entry_is_rejected_even_when_not_selected(self):
        from package_inventory import linked
        excluded = self.write(self.portable / 'settings.ini', b'private')
        prepared = replacement.prepared_runtime(self.portable)
        with patch('package_inventory.linked', side_effect=lambda path: path == excluded or linked(path)):
            with self.assertRaisesRegex(ValueError, 'Linked/reparse'):
                self.archive(prepared)
        self.assertFalse((self.output / 'portable.zip').exists())

    def test_launch_log_only_new_markers_and_no_free_form_secrets(self):
        self.write(self.log, b'[1] stage=main-entry\n[2] stage=std-exception extra=old-profile\n')
        baseline = replacement.launch_log_state(self.log)
        started = time.time_ns()
        with self.log.open('ab') as output:
            output.write(b'[3] stage=before-mainwindow\n'
                         b'[4] stage=std-exception extra=--token secret Authorization: Bearer hidden\n'
                         b'Authorization: Bearer raw-secret\n'
                         b'[5] stage=seh-unhandled extra=code=3221225477\n'
                         b'[6] stage=seh-unhandled extra=code=5 --token hidden\n'
                         b'[7] stage=before-qapp extra=--control-token=private\n')
        os.utime(self.log, ns=(started + 1, started + 1))
        result = replacement.preserve_launch_log(self.log, baseline, started, self.output)
        self.assertEqual(result['status'], 'saved')
        data = (self.output / 'launch.log').read_text()
        self.assertIn('code=3221225477', data)
        self.assertIn('extra=[redacted]', data)
        for forbidden in ('[1]', '[2]', 'old-profile', 'secret', 'Authorization', 'hidden', 'token', 'private'):
            self.assertNotIn(forbidden, data)

    def test_stale_unchanged_or_truncated_logs_are_omitted(self):
        self.write(self.log, b'[1] stage=before-mainwindow\n')
        os.utime(self.log, ns=(1, 1))
        baseline = replacement.launch_log_state(self.log)
        started = time.time_ns()
        self.assertEqual(replacement.preserve_launch_log(self.log, baseline, started, self.output)['reason'],
                         'predates-run')
        os.utime(self.log, ns=(started + 1, started + 1))
        self.assertEqual(replacement.preserve_launch_log(self.log, baseline, started, self.output)['reason'],
                         'no-new-markers')
        self.write(self.log, b'[2] stage=main-entry\n')
        os.utime(self.log, ns=(started + 1, started + 1))
        self.assertEqual(replacement.preserve_launch_log(self.log, baseline, started, self.output)['reason'],
                         'replaced-or-truncated')
        self.assertFalse((self.output / 'launch.log').exists())

    def test_new_log_is_capped_and_omits_unknown_or_incomplete_lines(self):
        started = time.time_ns()
        self.write(self.log, b'[1] stage=main-entry\n' * replacement.LAUNCH_LOG_LIMIT +
                   b'[2] stage=unknown-secret\n[3] stage=entering-exec\n[4] stage=main-entry')
        os.utime(self.log, ns=(started + 1, started + 1))
        result = replacement.preserve_launch_log(self.log, {'exists': False}, started, self.output)
        data = (self.output / 'launch.log').read_bytes()
        self.assertLessEqual(len(data), replacement.LAUNCH_LOG_LIMIT)
        self.assertEqual(result['bytes'], len(data))
        self.assertIn(b'[3]', data)
        self.assertNotIn(b'[2]', data)
        self.assertNotIn(b'[4]', data)

    def test_archive_error_still_preserves_safe_log_without_error_message(self):
        started = time.time_ns()
        self.write(self.log, b'[1] stage=main-entry\n')
        os.utime(self.log, ns=(started + 1, started + 1))
        prepared = replacement.prepared_runtime(self.portable)
        with patch.object(replacement, 'archive_failure_runtime', side_effect=OSError('Authorization: secret')):
            result = replacement.preserve_failure_evidence(
                self.runtime, self.portable, prepared, self.output, self.log, {'exists': False}, started)
        self.assertEqual(result['runtime'], {'status': 'unavailable', 'errorType': 'OSError'})
        self.assertEqual(result['launchLog']['status'], 'saved')
        metadata = self.output / result['directory'] / 'evidence.json'
        self.assertNotIn('secret', metadata.read_text())

    def test_only_guid_age_matched_declared_app_pdb_is_prepared(self):
        executable = self.write(self.root / 'build/SDR_Town.exe', b'application')
        doc = {'builtFiles': {'SDR_Town.exe': str(executable)}}
        self.assertEqual(replacement.prepare_app_pdb(doc, self.portable)['reason'], 'not-built')
        self.write(executable.with_suffix('.pdb'), b'matching-symbols')
        with patch.object(replacement, 'symbol_identity', side_effect=[(b'guid', 1), (b'guid', 2)]):
            with self.assertRaisesRegex(ValueError, 'GUID/age mismatch'):
                replacement.prepare_app_pdb(doc, self.portable)
        self.assertFalse((self.portable / 'SDR_Town.pdb').exists())
        with patch.object(replacement, 'symbol_identity', return_value=(b'guid', 1)):
            self.assertEqual(replacement.prepare_app_pdb(doc, self.portable)['status'], 'matched')
        prepared = replacement.prepared_runtime(self.portable, include_pdb=True)
        self.archive(prepared)
        with zipfile.ZipFile(self.output / 'portable.zip') as archive:
            self.assertEqual(archive.read('SDR_Town.pdb'), b'matching-symbols')

    def test_pdb_copy_refuses_resource_overflow_and_concurrent_growth(self):
        executable = self.write(self.root / 'build/SDR_Town.exe', b'application')
        candidate = self.write(executable.with_suffix('.pdb'), b'symbols')
        doc = {'builtFiles': {'SDR_Town.exe': str(executable)}}
        with patch.object(replacement, 'MAX_BYTES', 1):
            with self.assertRaisesRegex(ValueError, 'resource limit'):
                replacement.prepare_app_pdb(doc, self.portable)
        self.assertFalse((self.portable / 'SDR_Town.pdb').exists())

        def growing_pdb(path):
            if path == candidate:
                with candidate.open('ab') as output:
                    output.write(b'concurrent-growth')
            return b'guid', 1

        with patch.object(replacement, 'symbol_identity', side_effect=growing_pdb):
            with self.assertRaisesRegex(ValueError, 'grew during preparation'):
                replacement.prepare_app_pdb(doc, self.portable)
        self.assertLessEqual((self.portable / 'SDR_Town.pdb').stat().st_size, len(b'symbols'))

    def test_final_report_error_cannot_mask_qa_exception(self):
        stage = self.root / 'stage'
        stage.mkdir()
        failure = subprocess.CalledProcessError(3221225477, ['fixture.exe'])
        write_bytes = Path.write_bytes
        writes = 0

        def fail_second_write(path, data):
            nonlocal writes
            writes += 1
            if writes == 2:
                raise OSError('disk full')
            return write_bytes(path, data)

        with patch.object(replacement, 'perform_test', side_effect=failure), \
                patch.object(Path, 'write_bytes', fail_second_write):
            with self.assertRaises(subprocess.CalledProcessError) as caught:
                replacement.test({'qtVersion': '6.7.3'}, stage, self.output, self.root)
        self.assertIs(caught.exception, failure)

    def test_actual_harness_failure_retains_early_metadata_and_exact_runtime(self):
        stage = self.root / 'stage'
        executable = self.write(stage / 'SDR_Town.exe', b'application')
        for name in ('Qt6Core.dll', 'Qt6Svg.dll', 'platforms/qwindows.dll'):
            self.write(stage / name, b'original-qt')
        self.write(stage / 'qt.conf', b'[Paths]\nPlugins=.\n')
        self.write(stage / 'remote_diagnostics.json', b'PRIVATE')
        kit = self.write(stage / replacement.KIT, b'')
        with zipfile.ZipFile(kit, 'w') as archive:
            for module in ('qtbase', 'qtsvg'):
                archive.writestr('sources/' + replacement.archive_name(module, '6.7.3'), b'fixture')
        compiler = self.write(self.root / 'compiler/VC/Tools/bin/cl.exe', b'fixture')
        self.write(self.root / 'compiler/VC/Auxiliary/Build/vcvars64.bat', b'fixture')
        ninja = self.write(self.root / 'ninja.exe', b'fixture')
        (self.root / 'sdk/bin').mkdir(parents=True)
        for name in CONFIG_FILES:
            self.write(self.root / 'sdk' / name, b'#define QT_FEATURE_fixture 1\n')
        doc = {'qtVersion': '6.7.3', 'compiler': str(compiler), 'qtBin': str(self.root / 'sdk/bin'),
               'builtFiles': {'SDR_Town.exe': str(executable)}}
        failure = subprocess.CalledProcessError(3221225477, ['fixture-native-gui'])
        install = None
        native_calls = 0

        def fake_run(command, log, env=None, timeout=900):
            nonlocal install, native_calls
            command = list(map(str, command))
            log.write_text('fixture')
            for arg in command:
                if arg.startswith('-DCMAKE_INSTALL_PREFIX='):
                    install = Path(arg.split('=', 1)[1])
            if '--install' in command:
                for name in ('bin/Qt6Core.dll', 'bin/Qt6Svg.dll', 'plugins/platforms/qwindows.dll'):
                    self.write(install / name, b'rebuilt-qt')
                for name in CONFIG_FILES:
                    self.write(install / name, b'#define QT_FEATURE_fixture -1\n')
            if log.name == 'probe-build.log':
                self.write(Path(command[2]) / 'qt_replacement_probe.exe', b'probe')
            if log.name == 'cli.log':
                log.write_text('biastee <device>')
            if log.name == 'gui-native.log':
                native_calls += 1
                self.assertEqual(env['TEMP'], env['TMP'])
                self.assertTrue(Path(env['TEMP']).is_relative_to(self.root))
                self.write(Path(env['TEMP']) / 'sdr_town_launch.log',
                           b'[1] stage=seh-unhandled extra=code=3221225477\n')
                raise failure

        with patch.object(replacement, 'verify', return_value={'kitSha256': 'fixture'}), \
                patch.object(replacement, 'desktop_metrics', return_value={}), \
                patch.object(replacement.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, b'')), \
                patch.object(replacement.shutil, 'which', return_value=str(ninja)), \
                patch.object(replacement, 'extract_module'), patch.object(replacement, 'check_base_cache'), \
                patch.object(replacement, 'runtime_environment', return_value={'PATH': 'fixture'}), \
                patch.object(replacement, 'run', side_effect=fake_run):
            with self.assertRaises(subprocess.CalledProcessError) as caught:
                replacement.test(doc, stage, self.output, self.root, full=True, headless_layout=True)
            self.assertIs(caught.exception, failure)
            self.assertEqual(native_calls, 1)
            result = json.loads((self.output / 'result.json').read_bytes())
            self.assertFalse(install.parent.exists())
            # A second independent invocation simulates evidence storage failure,
            # not a retry in the harness. The original exception must still win.
            with patch.object(replacement, 'preserve_failure_evidence', side_effect=OSError('disk full')):
                with self.assertRaises(subprocess.CalledProcessError) as caught:
                    replacement.test(doc, stage, self.output, self.root, full=True, headless_layout=True)
        self.assertIs(caught.exception, failure)
        self.assertEqual(native_calls, 2)
        unavailable = json.loads((self.output / 'result.json').read_bytes())
        self.assertEqual(unavailable['failureEvidence'], {'status': 'unavailable', 'errorType': 'OSError'})
        self.assertEqual(unavailable['status'], 'failed')
        self.assertEqual(result['status'], 'failed')
        self.assertFalse(result['fullQtRebuildVerified'])
        self.assertEqual(result['sdkFeatureDifferences'][CONFIG_FILES[0]]['QT_FEATURE_fixture']['rebuilt'], '-1')
        self.assertEqual(result['rebuiltSha256']['Qt6Core.dll'], replacement.hashlib.sha256(b'rebuilt-qt').hexdigest())
        evidence = result['failureEvidence']
        self.assertEqual(evidence['launchLog']['status'], 'saved')
        with zipfile.ZipFile(self.output / evidence['directory'] / 'portable.zip') as archive:
            self.assertEqual(archive.read('Qt6Core.dll'), b'rebuilt-qt')
            self.assertNotIn(replacement.KIT, archive.namelist())
            self.assertNotIn('remote_diagnostics.json', archive.namelist())
        self.assertFalse(install.parent.exists())


if __name__ == '__main__':
    unittest.main()
