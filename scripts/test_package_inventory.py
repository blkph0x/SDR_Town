"""DEC-0172: disposable package fixtures, no dependency on private recordings."""
import json
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import package_inventory as inventory
import vcpkg_sources as sources
import embedded_notices as embedded
from test_embedded_notices import seed
from test_vcpkg_sources import fixture
import qt_sources as qt
import project_sources
from test_project_sources import fixture as project_fixture
from test_vcpkg_tooling import fixture as tooling_fixture
import vcpkg_tooling
from test_qt_sources import fixture as qt_fixture
from test_msvc_materials import fixture as msvc_fixture


class InventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        # Canonical Windows temp path, matching the staging/source fixtures.
        self.root = Path(self.temp.name).resolve()
        self.stage = self.root / 'build/deploy_staging'
        self.stage.mkdir(parents=True)
        self.archive = self.root / 'package.zip'
        self.sha = '1' * 40
        self.payloads = ('SDR_Town.exe', 'rtlsdr.dll', 'libusb-1.0.dll', 'Qt6Core.dll', 'sdrtown_rds_dsp.dll',
                         'sdrtown_sstv.exe', 'vcruntime140.dll', 'vc_redist.x64.exe',
                         'SoapyRTLSDR.dll', 'sdrPlaySupport.dll')
        for name in (*self.payloads, *inventory.required_notices(self.payloads)):
            self.write(name, b'fixture')
        self.source_doc, _, receipts, _ = fixture(self.root, {n: b'fixture' for n in self.payloads})
        ports = {p: inventory.port_info(receipts[p][0], p) for p in inventory.PORTS}
        self.inputs = {'schema': 1, 'sourceCommit': self.sha, 'qtVersion': '6.7.3', 'vcpkg': ports,
                       'vcpkgBinarySha256': {n: inventory.sha256(self.stage / n)
                                            for n in ('rtlsdr.dll', 'libusb-1.0.dll')}}
        project_blob, self.inputs['submodules'] = project_fixture(self.sha)
        self.write(project_sources.KIT, project_blob)
        self.write(vcpkg_tooling.KIT, tooling_fixture(self.sha))
        self.write(inventory.INPUTS, inventory.json_bytes(self.inputs))
        self.write('build-info.json', inventory.json_bytes({
            'sourceCommit': self.sha, 'executableSha256': inventory.sha256(self.stage / 'SDR_Town.exe')}))
        self.runtime = {'schema': 1, 'qtVersion': '6.7.3', 'windeployqtSha256': 'a' * 64,
                        'msvcRedistVersion': '14.44.35112',
                        'msvcRuntimeVersion': '14.44.35211.0',
                        'runtimeSha256': {n: inventory.sha256(self.stage / n)
                                          for n in ('Qt6Core.dll', 'vcruntime140.dll', 'vc_redist.x64.exe')}}
        self.write(inventory.RUNTIME_INPUTS, inventory.json_bytes(self.runtime))
        qt_doc, qt_cache, qt_pins = qt_fixture(self.root)
        patcher = patch.object(qt, 'PINS', qt_pins)
        patcher.start()
        self.addCleanup(patcher.stop)
        with patch('builtins.print'):
            qt.export(qt_doc, self.stage, qt_cache)
        for name, data in msvc_fixture(self.stage).items():
            self.write(name, data)
        with patch('builtins.print'):
            sources.export_kit(self.source_doc, self.stage)
        self.embedded_repo = self.root / 'embedded'
        seed(self.embedded_repo)
        for name, data in embedded.collect(self.embedded_repo, self.sha).items():
            self.write(name, data)

    def write(self, name, data):
        p = self.stage / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)

    def pack(self):
        with zipfile.ZipFile(self.archive, 'w', zipfile.ZIP_DEFLATED) as target:
            for p in sorted(self.stage.rglob('*')):
                if p.is_file():
                    target.write(p, p.relative_to(self.stage).as_posix())

    def test_roundtrip_deterministic_and_explicitly_blocked(self):
        doc = inventory.generate(self.stage)
        first = (self.stage / inventory.INVENTORY).read_bytes()
        inventory.generate(self.stage)
        self.assertEqual(first, (self.stage / inventory.INVENTORY).read_bytes())
        self.pack()
        self.assertEqual(doc, inventory.verify_zip(self.archive))
        self.assertEqual(len(doc['releaseBlockers']), 4)
        self.assertFalse(any('Microsoft publisher' in b for b in doc['releaseBlockers']))
        with self.assertRaisesRegex(ValueError, 'Publication blocked'):
            inventory.verify_zip(self.archive, require_publishable=True)

    def test_each_required_notice_missing_or_empty(self):
        inventory.generate(self.stage)
        for name in inventory.required_notices(self.payloads):
            with self.subTest(name=name):
                p = self.stage / name
                original = p.read_bytes()
                for missing in (False, True):
                    if missing:
                        p.unlink()
                    else:
                        p.write_bytes(b'')
                    with self.assertRaisesRegex(ValueError, 'Missing/empty required notice'):
                        inventory.generate(self.stage)
                p.write_bytes(original)

    def test_zip_payload_or_notice_tamper(self):
        inventory.generate(self.stage)
        for name in ('Qt6Core.dll', 'LICENSE.txt'):
            with self.subTest(name=name):
                p = self.stage / name
                original = p.read_bytes()
                p.write_bytes(original + b'tampered')
                self.pack()
                with self.assertRaisesRegex(ValueError, 'inventory mismatch'):
                    inventory.verify_zip(self.archive)
                p.write_bytes(original)

    def test_no_manifest_or_extra_missing_entry(self):
        self.pack()
        with self.assertRaisesRegex(ValueError, 'Missing package inventory'):
            inventory.verify_zip(self.archive)
        inventory.generate(self.stage)
        self.write('SdrTownControl.dll', b'extra')
        self.pack()
        with self.assertRaisesRegex(ValueError, 'inventory mismatch'):
            inventory.verify_zip(self.archive)
        (self.stage / 'SdrTownControl.dll').unlink()
        (self.stage / 'Qt6Core.dll').unlink()
        self.pack()
        with self.assertRaisesRegex(ValueError, 'inventory mismatch'):
            inventory.verify_zip(self.archive)

    def test_cannot_clear_blockers_by_editing_manifest(self):
        doc = inventory.generate(self.stage)
        doc['releaseBlockers'] = []
        self.write(inventory.INVENTORY, inventory.json_bytes(doc))
        self.pack()
        with self.assertRaisesRegex(ValueError, 'inventory mismatch'):
            inventory.verify_zip(self.archive, require_publishable=True)

    def test_runtime_must_match_configured_vcpkg_input(self):
        self.write('rtlsdr.dll', b'old DLL from another build')
        with self.assertRaisesRegex(ValueError, 'differs from installed'):
            inventory.generate(self.stage)

    def test_matching_local_hash_cannot_disguise_wrong_source_receipt(self):
        self.write('rtlsdr.dll', b'wrong binary but locally rehashed')
        self.inputs['vcpkgBinarySha256']['rtlsdr.dll'] = inventory.sha256(self.stage / 'rtlsdr.dll')
        self.write(inventory.INPUTS, inventory.json_bytes(self.inputs))
        with self.assertRaisesRegex(ValueError, 'differs from source receipt'):
            inventory.generate(self.stage)

    def test_source_kit_is_checked_inside_outer_zip(self):
        inventory.generate(self.stage)
        kit = self.stage / sources.KIT
        with zipfile.ZipFile(kit) as old:
            contents = {n: old.read(n) for n in old.namelist()}
        contents['ports/fmt/portfile.cmake'] += b'tampered'
        with zipfile.ZipFile(kit, 'w') as modified:
            for name, data in contents.items():
                modified.writestr(name, data)
        self.pack()
        with self.assertRaisesRegex(ValueError, 'Source checksum mismatch'):
            inventory.verify_zip(self.archive)

    def test_qt_and_microsoft_materials_are_checked_inside_outer_zip(self):
        for name in (qt.KIT, 'licenses/msvc/license.rtf'):
            with self.subTest(name=name):
                inventory.generate(self.stage)
                path = self.stage / name
                original = path.read_bytes()
                path.write_bytes(b'corrupt' if name == qt.KIT else original + b'tamper')
                self.pack()
                with self.assertRaises((ValueError, zipfile.BadZipFile)):
                    inventory.verify_zip(self.archive)
                path.write_bytes(original)

    def test_runtime_deployment_identity_and_hashes(self):
        for key, value, match in (('qtVersion', '6.11.1', 'Qt identity mismatch'),
                                  ('msvcRedistVersion', '', 'Invalid runtime'),
                                  ('windeployqtSha256', 'unknown', 'Invalid runtime'),
                                  ('runtimeSha256', {}, 'deployment inventory mismatch')):
            with self.subTest(key=key):
                self.write(inventory.RUNTIME_INPUTS, inventory.json_bytes(dict(self.runtime, **{key: value})))
                with self.assertRaisesRegex(ValueError, match):
                    inventory.generate(self.stage)
        self.write(inventory.RUNTIME_INPUTS, inventory.json_bytes(self.runtime))
        self.write('Qt6Core.dll', b'stale Qt DLL')
        with self.assertRaisesRegex(ValueError, 'deployment inventory mismatch'):
            inventory.generate(self.stage)

    def test_bandplan_and_embedded_notices_cannot_be_reinventoried_after_tamper(self):
        for name, match in (('data/inmarsat/4f2.json', 'Packaged embedded input mismatch'),
                            (embedded.NOTICES, 'notice text mismatch')):
            with self.subTest(name=name):
                path = self.stage / name
                original = path.read_bytes()
                inventory.generate(self.stage)
                path.write_bytes(original + b'tamper')
                with self.assertRaisesRegex(ValueError, match):
                    inventory.generate(self.stage)
                self.pack()
                with self.assertRaisesRegex(ValueError, match):
                    inventory.verify_zip(self.archive)
                path.write_bytes(original)

    def test_rtl_requires_transitive_usb_runtime(self):
        (self.stage / 'libusb-1.0.dll').unlink()
        with self.assertRaisesRegex(ValueError, 'missing required libusb'):
            inventory.generate(self.stage)

    def test_source_and_dependency_metadata_mismatch(self):
        for key, value, match in (('sourceCommit', '2' * 40, 'source commit mismatch'),
                                  ('qtVersion', 'unknown', 'Qt input version'),
                                  ('vcpkg', {}, 'Dependency metadata mismatch')):
            with self.subTest(key=key):
                self.write(inventory.INPUTS, inventory.json_bytes(dict(self.inputs, **{key: value})))
                with self.assertRaisesRegex(ValueError, match):
                    inventory.generate(self.stage)
        self.write(inventory.INPUTS, inventory.json_bytes(self.inputs))

    def test_unknown_runtime_vendor_driver_and_private_files(self):
        for name in ('old.dll', 'sdrplay_api.dll', 'capture.iq', 'private.key', 'test.exe',
                     'Qt6Unknown.dll', 'licenses/private.json'):
            with self.subTest(name=name):
                self.write(name, b'never publish')
                with self.assertRaisesRegex(ValueError, 'Uninventoried'):
                    inventory.generate(self.stage)
                (self.stage / name).unlink()

    def test_unsafe_zip_names_and_case_collision(self):
        inventory.generate(self.stage)
        for name in ('../escape', '/absolute', 'C:/escape', 'a\\b', 'a//b', 'a/./b',
                     'CON.txt', 'file.', 'a /b', 'qt6core.DLL'):
            with self.subTest(name=name):
                self.pack()
                with zipfile.ZipFile(self.archive, 'a') as target:
                    entry = zipfile.ZipInfo(name)
                    # ZipInfo normalizes backslashes on Windows; preserve the
                    # actual hostile archive filename for the reader fixture.
                    entry.filename = name
                    target.writestr(entry, b'unsafe')
                with self.assertRaisesRegex(ValueError, 'Unsafe|Duplicate'):
                    inventory.verify_zip(self.archive)

    def test_zip_symlink_and_file_directory_collision(self):
        inventory.generate(self.stage)
        self.pack()
        with zipfile.ZipFile(self.archive, 'a') as target:
            entry = zipfile.ZipInfo('licenses/link')
            entry.create_system = 3
            entry.external_attr = (stat.S_IFLNK | 0o777) << 16
            target.writestr(entry, '../../secret')
        with self.assertRaisesRegex(ValueError, 'Non-regular'):
            inventory.verify_zip(self.archive)
        self.pack()
        with zipfile.ZipFile(self.archive, 'a') as target:
            target.writestr('licenses', b'file not directory')
        with self.assertRaisesRegex(ValueError, 'file/directory collision'):
            inventory.verify_zip(self.archive)

    def test_resource_budgets(self):
        inventory.generate(self.stage)
        self.pack()
        for setting in ('MAX_BYTES', 'MAX_FILES', 'MAX_JSON'):
            with self.subTest(setting=setting), patch.object(inventory, setting, 1):
                with self.assertRaisesRegex(ValueError, 'limit exceeded'):
                    inventory.verify_zip(self.archive)

    def test_duplicate_json_keys(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate JSON key'):
            inventory.parse_json(b'{"schema":1,"schema":2}')

    def test_staging_reparse_rejected_before_copy(self):
        with patch.object(inventory, 'linked', return_value=True):
            with self.assertRaisesRegex(ValueError, 'non-linked'):
                inventory.stage_notices(self.root, self.stage, self.root, '6.7.3')

    def test_stage_notices_copies_exact_text_and_spdx(self):
        repo, vcpkg = self.root / 'repo', self.root / 'vcpkg'
        repo.mkdir()
        seed(repo)
        for name in (*inventory.ROOT_NOTICES, *inventory.SOURCE_NOTICES):
            p = repo / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b'exact upstream notice\r\n')
        for port in inventory.PORTS:
            for leaf in ('copyright', 'vcpkg.spdx.json'):
                p = vcpkg / 'share' / port / leaf
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes((self.stage / f'licenses/vcpkg/{port}/{leaf}').read_bytes())

        def fake_git(repo, *args):
            if args[0] == 'ls-tree':
                return f'160000 commit {self.sha}\t{args[-1]}'
            if args[0] == 'status':
                return ''
            return self.sha

        with patch.object(inventory, 'git', side_effect=fake_git):
            inventory.stage_notices(repo, self.stage, vcpkg, '6.7.3')
        for name in inventory.ROOT_NOTICES:
            self.assertEqual((self.stage / name).read_bytes(), (repo / name).read_bytes())
        data = json.loads((self.stage / inventory.INPUTS).read_text())
        self.assertEqual(data['submodules']['mbelib'], self.sha)
        self.assertNotIn(str(self.root), json.dumps(data))


if __name__ == '__main__':
    unittest.main()
