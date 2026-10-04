"""Disposable tooling provenance fixtures; no RF, compiler, or network."""
import hashlib
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

from package_inventory import json_bytes
import vcpkg_tooling as tooling


def zipped(files):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w') as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return buffer.getvalue()


def payloads():
    files = {'tooling/' + n: b'fixture\n' for n in tooling.ROOTS if '.' in n and '/' not in n}
    files.update({
        'tooling/scripts/buildsystems/vcpkg.cmake': b'# toolchain\n',
        'tooling/triplets/x64-windows.cmake': b'# triplet\n',
        'tooling/scripts/vcpkg-tool-metadata.txt': b'tool-version-sha=fixture\n',
    })
    for helper in tooling.HELPERS:
        prefix = 'installed/' + helper + '/'
        data = b'# installed helper\n'
        files[prefix + 'helper.cmake'] = data
        files[prefix + 'copyright'] = b'MIT fixture\n'
        files[prefix + 'vcpkg_abi_info.txt'] = b'triplet x64-windows\n'
        files['tooling/ports/' + helper + '/helper.cmake'] = data
        files[prefix + 'vcpkg.spdx.json'] = json_bytes({'files': [
            {'SPDXID': 'SPDXRef-binary-file-0', 'fileName': './share/' + helper + '/helper.cmake',
             'checksums': [{'algorithm': 'SHA256', 'checksumValue': hashlib.sha256(data).hexdigest()}]},
            {'SPDXID': 'SPDXRef-port-file-0', 'fileName': './helper.cmake',
             'checksums': [{'algorithm': 'SHA256', 'checksumValue': hashlib.sha256(data).hexdigest()}]},
        ]})
    return files


def fixture(source='1' * 40):
    files = payloads()
    files['manifest.json'] = json_bytes(tooling.manifest(files, source, '2' * 40))
    return zipped(files)


class ToolingTests(unittest.TestCase):
    def test_manifest_roundtrip_and_wrong_revision(self):
        result = tooling.verify(fixture(), '1' * 40)
        self.assertEqual(result['toolingCommit'], '2' * 40)
        with self.assertRaisesRegex(ValueError, 'manifest'):
            tooling.verify(fixture(), '3' * 40)

    def test_reject_missing_tampered_and_extra_inputs(self):
        for name in ('tooling/LICENSE.txt', 'tooling/scripts/buildsystems/vcpkg.cmake',
                     'installed/vcpkg-cmake/helper.cmake', 'installed/vcpkg-cmake/copyright'):
            for delete in (False, True):
                files = payloads()
                files['manifest.json'] = json_bytes(tooling.manifest(files, '1' * 40, '2' * 40))
                if delete:
                    del files[name]
                else:
                    files[name] += b'tampered'
                with self.subTest(name=name, delete=delete), self.assertRaises(ValueError):
                    tooling.verify(zipped(files), '1' * 40)
        files = payloads()
        files['secrets/token.txt'] = b'never allowed'
        files['manifest.json'] = json_bytes(tooling.manifest(files, '1' * 40, '2' * 40))
        with self.assertRaisesRegex(ValueError, 'Unexpected'):
            tooling.verify(zipped(files), '1' * 40)

    def test_receipt_still_fails_after_manifest_regenerated(self):
        for name in ('installed/vcpkg-cmake/helper.cmake', 'tooling/ports/vcpkg-cmake/helper.cmake'):
            files = payloads()
            files[name] += b'bad'
            files['manifest.json'] = json_bytes(tooling.manifest(files, '1' * 40, '2' * 40))
            with self.subTest(name=name), self.assertRaises(ValueError):
                tooling.verify(zipped(files), '1' * 40)

    def test_export_uses_committed_tooling_and_excludes_untracked(self):
        with tempfile.TemporaryDirectory(prefix='vcpkg-tooling-') as temp:
            root = Path(temp).resolve()
            repo, installed = root / 'repo', root / 'installed'
            repo.mkdir(); installed.mkdir()
            for name, data in payloads().items():
                base, relative = name.split('/', 1)
                path = repo / relative if base == 'tooling' else installed / 'share' / relative
                path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            def git(*args):
                subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
            git('init'); git('config', 'core.autocrlf', 'false'); git('add', '.')
            git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture')
            (repo / 'scripts/private.txt').write_text('must not ship')
            blob = tooling.collect(repo, installed, '1' * 40)
            with zipfile.ZipFile(io.BytesIO(blob)) as archive:
                self.assertNotIn('tooling/scripts/private.txt', archive.namelist())
            tooling.verify(blob, '1' * 40)
            (repo / 'scripts/buildsystems/vcpkg.cmake').write_text('modified')
            with self.assertRaisesRegex(ValueError, 'Modified'):
                tooling.collect(repo, installed, '1' * 40)

    def test_unsafe_duplicate_and_oversized_entries(self):
        for name in ('../outside', 'tooling/CON.txt', 'tooling/a\\b'):
            files = payloads(); files[name] = b'bad'
            with self.subTest(name=name), self.assertRaises(ValueError):
                tooling.verify(zipped(files), '1' * 40)
        from unittest.mock import patch
        with patch.object(tooling, 'MAX_BYTES', 10), self.assertRaisesRegex(ValueError, 'size'):
            tooling.verify(fixture(), '1' * 40)


if __name__ == '__main__':
    unittest.main()
