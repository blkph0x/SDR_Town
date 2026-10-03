"""Disposable staging fixtures; no user's build outputs are deleted or changed."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import stage_runtime as runtime
import msvc_materials
import qt_sources
from test_msvc_materials import fixture as msvc_fixture


class StagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sdr-stage-\u00fc ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.build = self.root / 'build'
        self.bin = self.build / 'bin/Release'
        self.bin.mkdir(parents=True)
        self.deps = self.root / 'configured-deps'
        self.deps.mkdir()
        self.stage = self.build / 'deploy_staging'
        self.doc = {'schema': 1, 'configuration': 'Release', 'buildRoot': str(self.build),
                    'vcpkgBin': str(self.deps), 'builtFiles': {}, 'moduleNotices': {}}
        for name in ('SDR_Town.exe', 'SdrTownControl.dll', 'sdr_aero_codec.dll',
                     'SoapyRTLSDR.dll', 'sdrPlaySupport.dll', 'sdrtown_rds_dsp.dll'):
            source = self.bin / name
            source.write_bytes(b'current-' + name.encode())
            self.doc['builtFiles'][name] = str(source)
        for name in ('rtlsdr.dll', 'libusb-1.0.dll', 'SoapySDR.dll', 'jansson.dll',
                     'z.dll', 'libsodium.dll', 'fmt.dll', 'spdlog.dll'):
            (self.deps / name).write_bytes(b'configured-' + name.encode())
        for module in ('SoapyRTLSDR', 'SoapySDRPlay3'):
            source = self.root / f'{module}.txt'
            source.write_bytes(b'notice')
            self.doc['moduleNotices'][f'licenses/{module}-LICENSE.txt'] = str(source)

    def test_stale_binaries_plugins_and_captures_are_not_imported_or_deleted(self):
        stale = ('pkgconf-7.dll', 'pthreadVC2.dll', 'Qt6Core.dll', 'recording.wav', 'old.exe',
                 'platforms/qwindows.dll', 'translations/old.qm', 'rtlsdr.dll')
        for name in stale:
            p = self.bin / name
            p.parent.mkdir(exist_ok=True)
            p.write_bytes(b'stale-must-survive')
        runtime.prepare(self.doc, self.stage, clean=True)
        runtime.prepare(self.doc, self.stage)
        for name in stale:
            self.assertEqual((self.bin / name).read_bytes(), b'stale-must-survive')
            if name == 'rtlsdr.dll':
                self.assertEqual((self.stage / name).read_bytes(), (self.deps / name).read_bytes())
            else:
                self.assertFalse((self.stage / name).exists())

    def test_missing_input_preserves_previous_stage(self):
        self.stage.mkdir()
        (self.stage / 'sentinel').write_bytes(b'keep')
        (self.deps / 'libusb-1.0.dll').unlink()
        with self.assertRaisesRegex(ValueError, 'Required configured dependency'):
            runtime.prepare(self.doc, self.stage, clean=True)
        self.assertEqual((self.stage / 'sentinel').read_bytes(), b'keep')

    def test_exact_destination_only_before_cleanup(self):
        for target in (self.root, self.build, self.bin, self.build / '../deploy_staging',
                       self.build / 'deploy_staging' / '..'):
            with self.subTest(target=target), self.assertRaises(ValueError):
                runtime.prepare(self.doc, target, clean=True)
        self.assertTrue(self.bin.is_dir())

    def test_redeploy_removes_only_previous_stage(self):
        runtime.prepare(self.doc, self.stage, clean=True)
        (self.stage / 'obsolete.dll').write_bytes(b'old')
        runtime.prepare(self.doc, self.stage, clean=True)
        runtime.prepare(self.doc, self.stage)
        self.assertFalse((self.stage / 'obsolete.dll').exists())
        self.assertTrue((self.bin / 'SDR_Town.exe').exists())

    def test_case_collision_unknown_and_traversal(self):
        for name in ('sdr_town.EXE', '../bad.dll', 'mystery.dll'):
            self.doc['builtFiles'][name] = self.doc['builtFiles']['SDR_Town.exe']
            with self.subTest(name=name), self.assertRaises(ValueError):
                runtime.prepare(self.doc, self.stage, clean=True)
            del self.doc['builtFiles'][name]
        self.assertFalse(self.stage.exists())

    def test_config_rejects_non_release_duplicate_keys_and_oversize(self):
        path = self.root / 'runtime-inputs.json'
        path.write_bytes(runtime.json_bytes(self.doc))
        self.assertEqual(runtime.config(path), self.doc)
        for data in (b'{"schema":1,"configuration":"Debug"}',
                     b'{"schema":1,"schema":1,"configuration":"Release"}'):
            path.write_bytes(data)
            with self.subTest(data=data), self.assertRaises(ValueError):
                runtime.config(path)
        with patch.object(runtime, 'MAX_JSON', 8):
            with self.assertRaisesRegex(ValueError, 'size limit'):
                runtime.config(path)
        self.assertFalse(self.stage.exists())

    def test_input_inside_destination_cannot_be_deleted(self):
        self.stage.mkdir()
        target = self.stage / 'SDR_Town.exe'
        target.write_bytes(b'keep')
        self.doc['builtFiles']['SDR_Town.exe'] = str(target)
        with self.assertRaisesRegex(ValueError, 'Input must not be inside'):
            runtime.prepare(self.doc, self.stage, clean=True)
        self.assertEqual(target.read_bytes(), b'keep')
        intermediate = self.build / 'intermediate'
        intermediate.mkdir()
        self.doc['builtFiles']['SDR_Town.exe'] = str(intermediate / '../deploy_staging/SDR_Town.exe')
        with self.assertRaisesRegex(ValueError, 'Input must not be inside'):
            runtime.prepare(self.doc, self.stage, clean=True)
        self.assertEqual(target.read_bytes(), b'keep')

    def test_linked_input_or_destination_is_rejected(self):
        link = self.root / 'linked'
        try:
            link.symlink_to(self.bin, target_is_directory=True)
        except OSError:
            self.skipTest('OS does not permit symlink creation')
        self.doc['builtFiles']['SDR_Town.exe'] = str(link / 'SDR_Town.exe')
        with self.assertRaisesRegex(ValueError, 'Linked runtime path'):
            runtime.prepare(self.doc, self.stage, clean=True)
        self.doc['builtFiles']['SDR_Town.exe'] = str(self.bin / 'SDR_Town.exe')
        self.stage.symlink_to(self.bin, target_is_directory=True)
        with self.assertRaises(ValueError):
            runtime.prepare(self.doc, self.stage, clean=True)
        self.assertTrue((self.bin / 'SDR_Town.exe').exists())

    def test_configured_qt_deployment_records_bounded_evidence(self):
        runtime.prepare(self.doc, self.stage, clean=True)
        runtime.prepare(self.doc, self.stage)
        qtbin = self.root / 'configured-qt/bin'
        qtbin.mkdir(parents=True)
        (qtbin / 'windeployqt.exe').write_bytes(b'tool')
        compiler = self.root / 'VS/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe'
        compiler.parent.mkdir(parents=True)
        compiler.write_bytes(b'compiler')
        crt = self.root / 'VS/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT'
        crt.mkdir(parents=True)
        for name in ('vcruntime140.dll', 'msvcp140.dll'):
            (crt / name).write_bytes(b'current CRT')
        self.doc.update(qtBin=str(qtbin), compiler=str(compiler), qtVersion='6.7.3')
        installer = crt.parent.parent / 'vc_redist.x64.exe'
        installer.write_bytes(b'installer fixture')
        materials = msvc_fixture(crt)

        def deploy(command, **kwargs):
            self.assertEqual(command[0], str(qtbin / 'windeployqt.exe'))
            self.assertEqual(command[command.index('--dir') + 1], str(self.stage))
            self.assertIn('--no-compiler-runtime', command)
            self.assertIn('--no-translations', command)
            self.assertEqual(kwargs['timeout'], 180)
            (self.stage / 'platforms').mkdir(exist_ok=True)
            (self.stage / 'platforms/qwindows.dll').write_bytes(b'Qt plugin')
            (self.stage / 'Qt6Core.dll').write_bytes(b'Qt core')

        with patch.object(msvc_materials, 'collect', return_value=(installer, materials)), \
                patch.object(qt_sources, 'export') as export:
            with patch.object(runtime.subprocess, 'run', side_effect=deploy):
                runtime.deploy_qt(self.doc, self.stage)
            export.assert_called_once()
            with patch.object(runtime.subprocess, 'run', side_effect=RuntimeError('Qt failed')):
                with self.assertRaisesRegex(RuntimeError, 'Qt failed'):
                    runtime.deploy_qt(self.doc, self.stage)
        evidence = (self.stage / 'licenses/runtime-deployment.json').read_bytes()
        self.assertNotIn(str(self.root).encode(), evidence)
        self.assertEqual(runtime.parse_json(evidence)['msvcRedistVersion'], '14.44.35112')
        self.assertEqual(runtime.parse_json(evidence)['msvcRuntimeVersion'], '14.44.35211.0')


if __name__ == '__main__':
    unittest.main()
