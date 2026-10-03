"""DEC-0172: disposable package fixtures, no dependency on private recordings."""
import json
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import package_inventory as inventory


class InventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stage = self.root / 'stage'
        self.stage.mkdir()
        self.archive = self.root / 'package.zip'
        self.sha = '1' * 40
        self.payloads = ('SDR_Town.exe', 'rtlsdr.dll', 'libusb-1.0.dll', 'Qt6Core.dll', 'sdrtown_rds_dsp.dll',
                         'sdrtown_sstv.exe', 'vcruntime140.dll', 'SoapyRTLSDR.dll', 'sdrPlaySupport.dll')
        for name in (*self.payloads, *inventory.required_notices(self.payloads)):
            self.write(name, b'fixture')
        ports = {}
        for port in inventory.PORTS:
            data = {'packages': [{'SPDXID': 'SPDXRef-port', 'name': port,
                                 'versionInfo': '1.2.3', 'licenseConcluded': 'NOASSERTION'}]}
            self.write(f'licenses/vcpkg/{port}/vcpkg.spdx.json', inventory.json_bytes(data))
            ports[port] = inventory.port_info(inventory.json_bytes(data), port)
        self.inputs = {'schema': 1, 'sourceCommit': self.sha, 'qtVersion': '6.7.3', 'vcpkg': ports,
                       'vcpkgBinarySha256': {n: inventory.sha256(self.stage / n)
                                            for n in ('rtlsdr.dll', 'libusb-1.0.dll')}}
        self.write(inventory.INPUTS, inventory.json_bytes(self.inputs))
        self.write('build-info.json', inventory.json_bytes({
            'sourceCommit': self.sha, 'executableSha256': inventory.sha256(self.stage / 'SDR_Town.exe')}))

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
        self.assertEqual(len(doc['releaseBlockers']), 5)
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
