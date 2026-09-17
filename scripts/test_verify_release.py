"""Positive and negative packaging gates using tiny disposable assets."""
import base64
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from verify_release import digest, verify


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.version = '1.2.3'
        self.installer = self.root / 'SDR_Town-1.2.3-win64-setup.exe'
        self.installer.write_bytes(b'installer fixture')
        self.portable = self.root / 'SDR_Town-1.2.3-win64-portable.zip'
        self.control = self.root / 'SdrTownControl-1.2.3-win64.dll'
        self.control.write_bytes(b'runtime fixture')
        self.staging = self.root / 'build/deploy_staging'
        names = ('SDR_Town.exe', 'SdrTownControl.dll', 'sdrtown_rds_dsp.dll', 'rtlsdr.dll',
                 'SoapySDR.dll', 'SoapyRTLSDR.dll', 'Qt6Core.dll', 'Qt6Widgets.dll',
                 'platforms/qwindows.dll', 'licenses/rtlsdr-COPYRIGHT.txt',
                 'sdrtown_sstv.exe', 'licenses/sstv/sstv-MIT.txt',
                 'licenses/sstv/libm-LICENSE.txt', 'licenses/sstv/rust-COPYRIGHT-library.html')
        with zipfile.ZipFile(self.portable, 'w') as archive:
            for name in names:
                path = self.staging / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'runtime fixture')
                archive.write(path, name)
        rtl = self.root / 'build/vcpkg_installed/x64-windows/bin/rtlsdr.dll'
        rtl.parent.mkdir(parents=True)
        rtl.write_bytes(b'runtime fixture')
        resources = self.root / 'resources'
        resources.mkdir()
        (resources / 'update_manifest_ed25519_pub.inc').write_text('"' + '01' * 32 + '"')
        (self.root / 'update.json.sig').write_text(base64.b64encode(bytes(64)).decode())
        self.manifest = {'version': self.version, 'tag': 'v1.2.3', 'installer': {
            'size': self.installer.stat().st_size, 'sha256': digest(self.installer),
            'url': 'https://github.com/Blkph0x/SDR_Town/releases/download/v1.2.3/SDR_Town-1.2.3-win64-setup.exe'}}
        self.write_manifest()
        self.write_sums()

    def write_manifest(self):
        (self.root / 'update.json').write_text(json.dumps(self.manifest))

    def write_sums(self):
        (self.root / 'SHA256SUMS.txt').write_text('\n'.join(
            f'{digest(p)}  {p.name}' for p in (self.installer, self.portable, self.control)))

    def run_verify(self):
        verify(self.root, self.version, self.installer)

    @patch('verify_release.subprocess.run')
    def test_valid_layout_invokes_signature_verifier(self, run):
        self.run_verify()
        self.assertIn('-verify', run.call_args.args[0])
        self.assertTrue(run.call_args.kwargs['check'])

    def test_installer_tamper(self):
        self.installer.write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'Installer size mismatch'):
            self.run_verify()

    def test_wrong_version(self):
        self.manifest['version'] = '9.9.9'
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'Manifest version mismatch'):
            self.run_verify()

    def test_wrong_download_url(self):
        self.manifest['installer']['url'] = 'https://example.invalid/setup.exe'
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'Unexpected installer URL'):
            self.run_verify()

    def test_private_file(self):
        with zipfile.ZipFile(self.portable, 'a') as archive:
            archive.writestr('private.pem', b'not a key')
        self.write_sums()
        with self.assertRaisesRegex(ValueError, 'Private/debug/capture artifact'):
            self.run_verify()

    def test_path_traversal(self):
        with zipfile.ZipFile(self.portable, 'a') as archive:
            archive.writestr('../outside', b'bad')
        self.write_sums()
        with self.assertRaisesRegex(ValueError, 'Unsafe ZIP path'):
            self.run_verify()

    def test_stale_rtl(self):
        (self.root / 'build/vcpkg_installed/x64-windows/bin/rtlsdr.dll').write_bytes(b'new runtime')
        with self.assertRaisesRegex(ValueError, 'RTL runtime differs'):
            self.run_verify()

    def test_staging_mismatch(self):
        (self.staging / 'SDR_Town.exe').write_bytes(b'other build')
        with self.assertRaisesRegex(ValueError, 'Staging mismatch'):
            self.run_verify()

    def test_checksum_mismatch(self):
        self.control.write_bytes(b'other module')
        with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
            self.run_verify()

    def test_short_signature(self):
        (self.root / 'update.json.sig').write_text(base64.b64encode(b'short').decode())
        with self.assertRaisesRegex(ValueError, 'Invalid signature size'):
            self.run_verify()

    @patch('verify_release.subprocess.run', side_effect=subprocess.CalledProcessError(1, 'openssl'))
    def test_bad_signature_is_fatal(self, _run):
        with self.assertRaises(subprocess.CalledProcessError):
            self.run_verify()


if __name__ == '__main__':
    unittest.main()
