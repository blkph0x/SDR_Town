"""Microsoft receipt fixtures: no installer execution and no licensing inference."""
import hashlib
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import msvc_materials as msvc
from package_inventory import json_bytes, sha256


def fixture(stage):
    license_data = b'{\\rtf1 exact license fixture}'
    files = {p.name: sha256(p) for p in stage.iterdir()
             if p.name in ('vcruntime140.dll', 'msvcp140.dll', 'vc_redist.x64.exe')}
    doc = {'schema': 1, 'directoryLabel': '14.44.35112', 'runtimeVersion': '14.44.35211.0',
           'fileVersions': {n: '14.44.35211.0' for n in files if n.endswith('.dll')},
           'runtimeSha256': files, 'installerSignature': {'status': 'Valid',
            'subject': 'CN=Microsoft Corporation, O=Microsoft Corporation', 'thumbprint': 'a' * 40},
           'licenseSha256': hashlib.sha256(license_data).hexdigest(),
           'licensePayload': {'member': 'u4', 'size': len(license_data),
                              'sha1': hashlib.sha1(license_data).hexdigest()}}
    return {msvc.EVIDENCE: json_bytes(doc), msvc.LICENSE: license_data, msvc.TERMS: msvc.README}


def manifest(**changes):
    attrs = dict(FilePath='license.rtf', FileSize='123', Hash='a' * 40,
                 Packaging='embedded', SourcePath='u4')
    attrs.update(changes)
    text = ' '.join(f'{k}="{v}"' for k, v in attrs.items())
    return (f'<BurnManifest xmlns="http://schemas.microsoft.com/wix/2008/Burn">'
            f'<UX><Payload {text}/></UX><Registration Version="14.44.35211.0"/>'
            '</BurnManifest>').encode()


class MsvcTests(unittest.TestCase):
    def test_collect_checks_signature_and_matching_binary_versions_without_executing_installer(self):
        with tempfile.TemporaryDirectory() as temp:
            redist = Path(temp) / '14.44.35112'
            redist.mkdir()
            installer, dll = redist / 'vc_redist.x64.exe', redist / 'msvcp140.dll'
            installer.write_bytes(b'fixture installer')
            dll.write_bytes(b'fixture dll')
            license_data = b'{\\rtf1 exact fixture}'
            xml = manifest(FileSize=str(len(license_data)), Hash=hashlib.sha1(license_data).hexdigest())
            signed = json_bytes({'status': 'Valid', 'subject': 'O=Microsoft Corporation', 'thumbprint': 'a' * 40})
            with patch.object(msvc.shutil, 'which', side_effect=lambda n: n), \
                    patch.object(msvc.subprocess, 'run', return_value=SimpleNamespace(stdout=signed)) as run, \
                    patch.object(msvc, 'extract', side_effect=lambda t, i, m, b: xml if m == '0' else license_data), \
                    patch.object(msvc, 'file_version', return_value='14.44.35211.0') as versions:
                origin, data = msvc.collect(redist, {'msvcp140.dll': dll})
                self.assertEqual(origin, installer.resolve())
                self.assertEqual(data[msvc.LICENSE], license_data)
                self.assertEqual(run.call_args.args[0][0], 'powershell.exe')
                self.assertNotEqual(run.call_args.args[0][0], str(installer))
                versions.return_value = '14.44.35112.0'
                with self.assertRaisesRegex(ValueError, 'DLL/bundle version mismatch'):
                    msvc.collect(redist, {'msvcp140.dll': dll})

    def test_bundle_version_is_not_directory_version_and_payload_is_discovered(self):
        self.assertEqual(msvc.payload(manifest(SourcePath='u19')),
                         ('14.44.35211.0', 'u19', 123, 'a' * 40))

    def test_missing_ambiguous_unsafe_or_large_payload_rejected(self):
        for xml in (manifest(FilePath='another.rtf'), manifest(SourcePath='../outside'),
                    manifest(Packaging='external'), manifest(FileSize='0'),
                    manifest(FileSize=str(msvc.MAX_JSON + 1)), manifest(Hash='bad'),
                    manifest().replace(b'</UX>', b'<Payload FilePath="license.rtf"/></UX>'),
                    b'<!DOCTYPE evil>' + manifest()):
            with self.subTest(xml=xml), self.assertRaises(ValueError):
                msvc.payload(xml)

    def test_runtime_and_license_receipt_tampering(self):
        with tempfile.TemporaryDirectory() as temp:
            stage = Path(temp)
            for name in ('vcruntime140.dll', 'vc_redist.x64.exe'):
                (stage / name).write_bytes(b'fixture')
            files = fixture(stage)
            entries = {p.name: {'sha256': sha256(p), 'component': 'msvc-runtime'} for p in stage.iterdir()}
            deployment = {'msvcRedistVersion': '14.44.35112', 'msvcRuntimeVersion': '14.44.35211.0'}
            result = msvc.verify(files.__getitem__, entries, deployment)
            self.assertEqual(result['runtimeVersion'], '14.44.35211.0')
            self.assertFalse(result['publisherEntitlementVerified'])
            for name, expected in ((msvc.LICENSE, 'license checksum'), (msvc.TERMS, 'distinction')):
                original = files[name]
                files[name] += b'tamper'
                with self.assertRaisesRegex(ValueError, expected):
                    msvc.verify(files.__getitem__, entries, deployment)
                files[name] = original
            entries['vcruntime140.dll']['sha256'] = '0' * 64
            with self.assertRaisesRegex(ValueError, 'runtime checksum'):
                msvc.verify(files.__getitem__, entries, deployment)


if __name__ == '__main__':
    unittest.main()
