"""Finite publication gates: qualify exact inputs; reject stale or partial evidence."""
import copy
import io
from pathlib import Path
import unittest
from unittest.mock import patch
import zipfile

import distribution_materials as materials
import qualify_distribution as qualification
import package_inventory as inventory
from test_qt_sources import tar_bytes


def zipped(files):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in files.items():
            z.writestr(name, data)
    return stream.getvalue()


def qualification_fixture():
    names = ('SDR_Town.exe', 'Qt6Core.dll', 'licenses/qt/source-materials.zip',
             'licenses/vcpkg/source-materials.zip', 'licenses/vcpkg/tooling-materials.zip',
             *qualification.USB)
    entries = {n: {'sha256': materials.digest(n.encode())} for n in names}
    inputs = {'sourceCommit': 'a' * 40, 'qtVersion': '6.7.3'}
    common = {'status': 'pass', 'originalPackageUnchanged': True,
              'applicationSha256': entries['SDR_Town.exe']['sha256']}
    qt = dict(common, fullQtRebuildVerified=True, qtVersion='6.7.3',
              sourceKitSha256=entries['licenses/qt/source-materials.zip']['sha256'],
              rebuiltSha256={'Qt6Core.dll': '1' * 64}, applicationCli='pass',
              originalQtSha256={'Qt6Core.dll': entries['Qt6Core.dll']['sha256']},
              nativeTlsBackend='pass', rendererAndPlugins='pass', widgetsAndLoopbackNetwork='pass',
              applicationGui='four-no-rx-profiles-pass')
    usb = dict(common, sourceCommit=inputs['sourceCommit'], binaryCache='disabled',
               loader='pass', cli='pass',
               sourceKitSha256=entries['licenses/vcpkg/source-materials.zip']['sha256'],
               toolingKitSha256=entries['licenses/vcpkg/tooling-materials.zip']['sha256'],
               originalRuntimeSha256={n: entries[n]['sha256'] for n in qualification.USB},
               rebuiltRuntimeSha256={n: '2' * 64 for n in qualification.USB})
    return {'schema': 1, 'sourceCommit': inputs['sourceCommit'], 'qt': qt, 'usb': usb}, entries, inputs


class DistributionTests(unittest.TestCase):
    def test_licence_texts_have_the_reviewed_unmodified_hashes(self):
        root = Path(__file__).resolve().parents[1]
        for name, expected in materials.LICENSES.items():
            self.assertEqual(materials.digest((root / 'resources/licenses' / name).read_bytes()), expected)

    def test_exact_package_qualification_passes(self):
        record, entries, inputs = qualification_fixture()
        self.assertEqual(qualification.verify(record, entries, inputs), record)

    def test_qualification_rejects_wrong_package_source_version_and_missing_plugins(self):
        record, entries, inputs = qualification_fixture()
        for path, value in ((('sourceCommit',), 'b' * 40),
                            (('qt', 'applicationSha256'), '0' * 64),
                            (('usb', 'applicationSha256'), '0' * 64),
                            (('qt', 'qtVersion'), '6.11.1'),
                            (('qt', 'rebuiltSha256'), {}),
                            (('qt', 'originalQtSha256'), {}),
                            (('usb', 'rebuiltRuntimeSha256'), {}),
                            (('usb', 'originalRuntimeSha256'), {}),
                            (('usb', 'sourceKitSha256'), '0' * 64),
                            (('usb', 'toolingKitSha256'), '0' * 64),
                            (('qt', 'sourceKitSha256'), '0' * 64)):
            altered = copy.deepcopy(record)
            target = altered
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(ValueError):
                qualification.verify(altered, entries, inputs)

    def test_incomplete_rebuild_cached_binaries_and_failed_tests_cannot_qualify(self):
        record, entries, inputs = qualification_fixture()
        for section, key, value in (
            ('qt', 'status', 'failed'), ('usb', 'status', 'running'),
            ('qt', 'originalPackageUnchanged', False), ('usb', 'originalPackageUnchanged', False),
            ('qt', 'fullQtRebuildVerified', False), ('usb', 'binaryCache', 'enabled'),
            ('qt', 'applicationGui', 'one-profile-pass'), ('qt', 'nativeTlsBackend', 'failed'),
            ('usb', 'loader', 'failed'), ('usb', 'cli', 'failed')):
            altered = copy.deepcopy(record)
            altered[section][key] = value
            with self.subTest(section=section, key=key), self.assertRaises(ValueError):
                qualification.verify(altered, entries, inputs)

    def test_qt_selects_ftl_and_does_not_include_test_modules_in_runtime_scope(self):
        rows = [{'Id': 'xsvg', 'QDocModule': 'qtsvg', 'LicenseId': 'HPND-sell-variant'},
                {'Id': 'freetype', 'QDocModule': 'qtgui', 'LicenseId': 'FTL OR GPL-2.0-only'},
                {'Id': 'fixture-test', 'QDocModule': 'qttestlib', 'LicenseId': 'BSD-4-Clause'}]
        raw = zipped({'sources/qtsvg.tar.xz': tar_bytes('qtsvg', '6.7.3', {
            'src/qt_attribution.json': inventory.json_bytes(rows)})})
        actual = materials.qt_review(raw)
        self.assertEqual(actual[1]['selection'], 'FTL')
        self.assertFalse(actual[2]['runtimeCandidate'])

    def test_new_runtime_licence_requires_review(self):
        rows = [{'Id': 'xsvg', 'QDocModule': 'qtsvg', 'LicenseId': 'proprietary-fixture'}]
        raw = zipped({'sources/qtsvg.tar.xz': tar_bytes('qtsvg', '6.7.3', {
            'src/qt_attribution.json': inventory.json_bytes(rows)})})
        with self.assertRaisesRegex(ValueError, 'Unreviewed Qt'):
            materials.qt_review(raw)

    def test_embedded_sources_missing_notices_or_new_code_fail(self):
        files = {'source/' + root + '/' + notice: b'fixture original notice'
                 for root, (_, notice) in materials.EMBEDDED.items()}
        self.assertEqual(len(materials.source_review(zipped(files))), len(materials.EMBEDDED))
        with self.assertRaisesRegex(ValueError, 'Unreviewed embedded'):
            materials.source_review(zipped(dict(files, **{'source/external/unknown/foreign.c': b'code'})))
        files.pop('source/external/miniaudio/LICENSE')
        with self.assertRaisesRegex(ValueError, 'Missing reviewed source notice'):
            materials.source_review(zipped(files))

    def test_materials_cannot_be_cleared_by_qualification_alone(self):
        record, entries, inputs = qualification_fixture()
        entries[qualification.QUALIFICATION] = {'sha256': '0' * 64}
        self.assertTrue(inventory.blockers(entries))

    def test_checked_materials_still_require_exact_package_qualification(self):
        record, entries, inputs = qualification_fixture()
        entries[materials.KIT] = {'sha256': '0' * 64}
        with patch.object(materials, 'verify') as check:
            self.assertEqual(len(inventory.blockers(entries, lambda *a: b'fixture', inputs)), 1)
            check.assert_called_once()
            entries[qualification.QUALIFICATION] = {'sha256': '0' * 64}
            read = lambda name, *a: inventory.json_bytes(record) if name == qualification.QUALIFICATION else b'fixture'
            self.assertEqual(inventory.blockers(entries, read, inputs), [])
            record['usb']['loader'] = 'failed'
            with self.assertRaisesRegex(ValueError, 'USB qualification'):
                inventory.blockers(entries, read, inputs)


if __name__ == '__main__':
    unittest.main()
