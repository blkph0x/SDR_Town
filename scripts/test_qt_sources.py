"""Disposable Qt source-kit fixtures; no network, compiler or SDK modification."""
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import qt_sources as qt


def tar_bytes(module, version, items=None):
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode='w:xz') as archive:
        for name, data in (items or {'LICENSES/LGPL-3.0.txt': b'license fixture',
                                    'src/3rdparty/a/qt_attribution.json': b'{"Id":"fixture"}',
                                    'src/source.cpp': b'complete source fixture'}).items():
            entry = tarfile.TarInfo(f'{module}-everywhere-src-{version}/{name}')
            entry.size = len(data)
            archive.addfile(entry, io.BytesIO(data))
    return stream.getvalue()


def fixture(root, version='6.7.3'):
    cache, sdk = root / 'qt-cache', root / 'qt-sdk'
    cache.mkdir(exist_ok=True)
    (sdk / 'bin').mkdir(parents=True, exist_ok=True)
    checksums = []
    for module in qt.MODULES:
        data = tar_bytes(module, version)
        (cache / qt.archive_name(module, version)).write_bytes(data)
        checksums.append(qt.digest(data))
    for name in qt.CONFIG_FILES:
        target = sdk / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((f'QT_VERSION = {version}\n' if name.endswith('.pri') else 'fixture\n').encode())
    return {'qtVersion': version, 'qtBin': str(sdk / 'bin')}, cache, {version: tuple(checksums)}


class QtSourceTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        self.stage = self.root / 'stage'
        self.stage.mkdir()
        self.doc, self.cache, pins = fixture(self.root)
        self.patcher = patch.object(qt, 'PINS', pins)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)

    def export(self):
        with patch('builtins.print'):
            qt.export(self.doc, self.stage, self.cache)
        return (self.stage / qt.KIT).read_bytes()

    def mutate(self, changes):
        data = self.export()
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            files = {n: z.read(n) for n in z.namelist()}
        changes(files)
        # Even updating all outer hashes cannot substitute an unpinned source.
        doc = qt.parse_json(files['manifest.json'])
        doc['files'] = {n: qt.digest(d) for n, d in files.items() if n != 'manifest.json'}
        files['manifest.json'] = qt.json_bytes(doc)
        out = io.BytesIO()
        with zipfile.ZipFile(out, 'w') as z:
            for n, data in files.items():
                entry = zipfile.ZipInfo(n)
                entry.external_attr = 0o100644 << 16
                z.writestr(entry, data)
        return out.getvalue()

    def test_deterministic_exact_version_and_notice_catalog(self):
        data = self.export()
        self.assertEqual(data, self.export())
        result = qt.verify(data, '6.7.3')
        self.assertEqual(result['archiveCount'], 3)
        self.assertEqual(result['noticeCount'], 6)
        self.assertFalse(result['fullRebuildVerified'])
        self.assertNotIn(str(self.root).encode(), data)

    def test_unknown_version_and_missing_source(self):
        with self.assertRaisesRegex(ValueError, 'no reviewed source pins'):
            qt.pins('9.9.9')
        (self.cache / qt.archive_name('qtsvg', '6.7.3')).unlink()
        with self.assertRaisesRegex(ValueError, 'Missing runtime input'):
            self.export()

    def test_wrong_sdk_version(self):
        (self.root / 'qt-sdk/mkspecs/qconfig.pri').write_bytes(b'QT_VERSION = 6.11.1\n')
        with self.assertRaisesRegex(ValueError, 'SDK version mismatch'):
            self.export()

    def test_tampered_cached_archive_cannot_be_used_or_silently_redownloaded(self):
        p = self.cache / qt.archive_name('qtbase', '6.7.3')
        p.write_bytes(b'tamper')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            self.export()
        with patch.object(qt.urllib.request, 'urlopen') as network:
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                qt.fetch('6.7.3', self.cache)
            network.assert_not_called()

    def test_reinventoried_source_and_notices_rejected(self):
        for name, message in (('sources/' + qt.archive_name('qtbase', '6.7.3'), 'source checksum'),
                              ('notices/qtbase.txt', 'catalog mismatch'),
                              ('REBUILD.md', 'instructions mismatch')):
            with self.subTest(name=name):
                changed = self.mutate(lambda files: files.__setitem__(name, files[name] + b'tamper'))
                with self.assertRaisesRegex(ValueError, message):
                    qt.verify(changed, '6.7.3')

    def test_hostile_archive_path_and_limits(self):
        for name in ('../escape', '/absolute', 'C:/escape', 'a\\b'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'Unsafe'):
                qt.catalog(tar_bytes('qtbase', '6.7.3', {name: b'x'}), 'qtbase', '6.7.3')
        with patch.object(qt, 'MAX_EXPANDED', 1):
            with self.assertRaisesRegex(ValueError, 'resource limit'):
                qt.catalog(tar_bytes('qtbase', '6.7.3'), 'qtbase', '6.7.3')

    def test_linked_source_archive_member(self):
        out = io.BytesIO()
        with tarfile.open(fileobj=out, mode='w:xz') as t:
            i = tarfile.TarInfo('qtbase-everywhere-src-6.7.3/link')
            i.type, i.linkname = tarfile.SYMTYPE, '../../outside'
            t.addfile(i)
        with self.assertRaisesRegex(ValueError, 'Non-regular'):
            qt.catalog(out.getvalue(), 'qtbase', '6.7.3')

    def test_kit_extra_entry_and_size_limit(self):
        data = self.mutate(lambda files: files.__setitem__('private.txt', b'no'))
        with self.assertRaisesRegex(ValueError, 'inventory mismatch'):
            qt.verify(data, '6.7.3')
        data = self.export()
        with patch.object(qt, 'MAX_KIT', 1):
            with self.assertRaisesRegex(ValueError, 'size limit'):
                qt.verify(data, '6.7.3')


if __name__ == '__main__':
    unittest.main()
