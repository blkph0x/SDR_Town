"""Disposable exact-source fixtures; no compiler invocation, RF or downloads."""
import io
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import package_inventory as inventory
import vcpkg_sources as sources


def zipped(files):
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w') as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return out.getvalue()


def fixture(root, binaries=None):
    build = root / 'build'
    stage = build / 'deploy_staging'
    stage.mkdir(parents=True, exist_ok=True)
    vcpkg = root / 'vcpkg'
    cache = vcpkg / 'downloads'
    cache.mkdir(parents=True)
    receipts, ports = {}, {}
    for port in inventory.PORTS:
        recipe = {'portfile.cmake': b'# exact fixture recipe\n',
                  'vcpkg.json': inventory.json_bytes({'name': port, 'version': '1.2.3'})}
        records = []
        for i, (name, data) in enumerate(recipe.items()):
            path = vcpkg / 'ports' / port / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            records.append({'SPDXID': f'SPDXRef-port-file-{i}', 'fileName': './' + name,
                            'checksums': [{'algorithm': 'SHA256', 'checksumValue': sources.digest(data)}]})
        for name, data in (binaries or {}).items():
            if inventory.VCPKG_DLLS.get(name.lower()) == port:
                records.append({'SPDXID': f'SPDXRef-binary-file-{len(records)}', 'fileName': './bin/' + name,
                                'checksums': [{'algorithm': 'SHA256', 'checksumValue': sources.digest(data)}]})
        archive = zipped({'COPYING': ('test source ' + port).encode()})
        (cache / f'{port}.zip').write_bytes(archive)
        doc = {'packages': [{'SPDXID': 'SPDXRef-port', 'name': port, 'versionInfo': '1.2.3',
                             'licenseConcluded': 'NOASSERTION'},
                            {'SPDXID': 'SPDXRef-resource-0',
                             'checksums': [{'algorithm': 'SHA512', 'checksumValue': sources.digest(archive, 'sha512')}]}],
               'files': records}
        spdx = inventory.json_bytes(doc)
        receipts[port] = spdx, b'fixture notice\n'
        ports[port] = inventory.port_info(spdx, port)
        for leaf, data in (('vcpkg.spdx.json', spdx), ('copyright', receipts[port][1])):
            dest = stage / 'licenses/vcpkg' / port / leaf
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
    commit = '1' * 40
    (stage / inventory.INPUTS).write_bytes(inventory.json_bytes({'sourceCommit': commit, 'vcpkg': ports}))
    doc = {'buildRoot': str(build), 'vcpkgRoot': str(vcpkg)}
    return doc, stage, receipts, commit


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sources-\u00fc ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.doc, self.stage, self.receipts, self.commit = fixture(self.root)
        self.target = self.stage / sources.KIT

    def export(self):
        with patch('builtins.print'):
            return sources.export_kit(self.doc, self.stage)

    def verify(self, blob=None):
        return sources.verify_kit(blob or self.target.read_bytes(), self.receipts, self.commit)

    def payloads(self):
        with zipfile.ZipFile(self.target) as archive:
            return {n: archive.read(n) for n in archive.namelist()}

    def test_exact_materials_and_deterministic_roundtrip(self):
        unrelated = self.root / 'vcpkg/ports/fmt/capture.wav'
        unrelated.write_bytes(b'never include this')
        self.assertEqual(self.export(), {'ports': 10, 'archives': 10, 'downloadedPatches': 0, 'recipeFiles': 20})
        first = self.target.read_bytes()
        self.export()
        self.assertEqual(self.target.read_bytes(), first)
        self.assertEqual(self.verify()['archives'], 10)
        self.assertNotIn(b'never include this', first)
        self.assertEqual(unrelated.read_bytes(), b'never include this')

    def test_changed_or_missing_recipe_preserves_old_kit(self):
        self.export()
        old = self.target.read_bytes()
        path = self.root / 'vcpkg/ports/fmt/portfile.cmake'
        path.write_bytes(b'different recipe')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            self.export()
        path.unlink()
        with self.assertRaisesRegex(ValueError, 'Missing runtime input'):
            self.export()
        self.assertEqual(self.target.read_bytes(), old)

    def test_same_name_wrong_archive_is_not_source(self):
        self.export()
        old = self.target.read_bytes()
        (self.root / 'vcpkg/downloads/fmt.zip').write_bytes(zipped({'COPYING': b'different version'}))
        with self.assertRaisesRegex(ValueError, 'Missing exact source'):
            self.export()
        self.assertEqual(self.target.read_bytes(), old)

    def test_downloaded_patch_is_required_and_hash_verified(self):
        patch_data = b'From fixture\n\ndiff --git a/a b/a\n--- a/a\n+++ b/a\n@@ -1 +1 @@\n-old\n+new\n'
        path = self.root / 'vcpkg/downloads/fmt-fix.patch'
        path.write_bytes(patch_data)
        doc = inventory.parse_json(self.receipts['fmt'][0])
        doc['packages'].append({'SPDXID': 'SPDXRef-resource-1', 'checksums': [
            {'algorithm': 'SHA512', 'checksumValue': sources.digest(patch_data, 'sha512')}]})
        self.receipts['fmt'] = inventory.json_bytes(doc), self.receipts['fmt'][1]
        (self.stage / 'licenses/vcpkg/fmt/vcpkg.spdx.json').write_bytes(self.receipts['fmt'][0])
        summary = self.export()
        self.assertEqual(summary['archives'], 10)
        self.assertEqual(summary['downloadedPatches'], 1)
        self.assertEqual(self.verify(), summary)
        old = self.target.read_bytes()
        path.write_bytes(patch_data + b'wrong')
        with self.assertRaisesRegex(ValueError, 'Missing exact source'):
            self.export()
        path.unlink()
        with self.assertRaisesRegex(ValueError, 'Missing exact source'):
            self.export()
        self.assertEqual(self.target.read_bytes(), old)

    def test_missing_or_duplicate_source_checksum_rejected(self):
        original = inventory.parse_json(self.receipts['fmt'][0])
        for checksums in ([], original['packages'][1]['checksums'] * 2):
            original['packages'][1]['checksums'] = checksums
            receipts = dict(self.receipts, fmt=(inventory.json_bytes(original), b'notice'))
            with self.assertRaisesRegex(ValueError, 'source checksum'):
                sources.definitions(receipts)

    def test_unsafe_duplicate_or_incomplete_recipe_rejected(self):
        original = self.receipts['fmt'][0]
        for name in ('../escape', 'C:/escape', './a\\escape', './PORTFILE.CMAKE', './CON.txt'):
            doc = inventory.parse_json(original)
            doc['files'][1]['fileName'] = name
            with self.subTest(name=name), self.assertRaises(ValueError):
                sources.definitions(dict(self.receipts, fmt=(inventory.json_bytes(doc), b'notice')))
        doc = inventory.parse_json(original)
        doc['files'].append(doc['files'][0])
        with self.assertRaisesRegex(ValueError, 'SPDX identity'):
            sources.definitions(dict(self.receipts, fmt=(inventory.json_bytes(doc), b'notice')))
        doc = inventory.parse_json(original)
        doc['files'].append(dict(doc['files'][0], SPDXID='SPDXRef-port-file-2',
                                 fileName='./portfile.cmake/child'))
        with self.assertRaisesRegex(ValueError, 'file/directory collision'):
            sources.definitions(dict(self.receipts, fmt=(inventory.json_bytes(doc), b'notice')))

    def test_nested_source_archive_and_recipe_tamper(self):
        self.export()
        for name in ('ports/fmt/portfile.cmake', 'archives/fmt/0.zip', 'receipts/fmt/copyright'):
            payloads = self.payloads()
            payloads[name] += b'changed'
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                self.verify(zipped(payloads))

    def test_malformed_receipts_and_bounded_reads(self):
        for invalid in ([], {}, {'files': [None], 'packages': []}):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                sources.definitions(dict(self.receipts, fmt=(inventory.json_bytes(invalid), b'notice')))
        doc = inventory.parse_json(self.receipts['fmt'][0])
        doc['files'][0]['fileName'] = 17
        with self.assertRaisesRegex(ValueError, 'relative recipe'):
            sources.definitions(dict(self.receipts, fmt=(inventory.json_bytes(doc), b'notice')))
        path = self.root / 'bounded.bin'
        path.write_bytes(b'12345')
        with self.assertRaisesRegex(ValueError, 'size limit'):
            sources.read_bounded(path, 4)
        self.assertEqual(sources.read_bounded(path, 5), b'12345')

    def test_membership_manifest_and_source_commit(self):
        self.export()
        for name in ('private-key.txt', '../outside', 'ports/fmt/unlisted.patch'):
            payloads = self.payloads()
            payloads[name] = b'extra'
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.verify(zipped(payloads))
        payloads = self.payloads()
        del payloads['ports/fmt/portfile.cmake']
        with self.assertRaisesRegex(ValueError, 'membership'):
            self.verify(zipped(payloads))
        payloads = self.payloads()
        payloads['manifest.json'] = b'{}'
        with self.assertRaisesRegex(ValueError, 'manifest'):
            self.verify(zipped(payloads))
        with self.assertRaisesRegex(ValueError, 'manifest'):
            sources.verify_kit(self.target.read_bytes(), self.receipts, '2' * 40)

    def test_linked_source_or_destination_rejected(self):
        source = self.root / 'vcpkg/ports/fmt/portfile.cmake'
        backup = source.with_suffix('.original')
        source.rename(backup)
        try:
            source.symlink_to(backup)
        except OSError:
            self.skipTest('OS does not permit symbolic links')
        with self.assertRaisesRegex(ValueError, 'Linked runtime'):
            self.export()
        source.unlink()
        backup.rename(source)
        self.target.symlink_to(backup)
        with self.assertRaisesRegex(ValueError, 'Linked/reparse'):
            self.export()
        self.assertFalse(backup.exists())
        self.assertTrue(source.exists())

    def test_atomic_write_failure_preserves_existing_kit(self):
        self.export()
        old = self.target.read_bytes()
        with patch.object(sources.os, 'replace', side_effect=OSError('fixture denied')):
            with self.assertRaisesRegex(OSError, 'fixture denied'):
                self.export()
        self.assertEqual(self.target.read_bytes(), old)
        self.assertFalse(list(self.target.parent.glob('.source-*.tmp')))

    def test_nested_zip_entry_bounds_duplicates_and_links(self):
        self.export()
        blob = self.target.read_bytes()
        with patch.object(sources, 'MAX_KIT', len(blob) - 1):
            with self.assertRaisesRegex(ValueError, 'size limit'):
                self.verify(blob)
        with patch.object(sources, 'MAX_MEMBERS', 2):
            with self.assertRaisesRegex(ValueError, 'member limit|resource limit'):
                self.verify(blob)
        for name, mode in (('PORTS/fmt/portfile.cmake', stat.S_IFREG), ('linked', stat.S_IFLNK)):
            out = io.BytesIO(blob)
            with zipfile.ZipFile(out, 'a') as archive:
                item = zipfile.ZipInfo(name)
                item.create_system = 3
                item.external_attr = (mode | 0o644) << 16
                archive.writestr(item, b'bad')
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.verify(out.getvalue())


if __name__ == '__main__':
    unittest.main()
