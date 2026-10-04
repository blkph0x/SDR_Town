"""Source distribution tests using disposable repositories, never private IQ."""
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

import project_sources as sources
from package_inventory import json_bytes


def fixture(revision='1'*40):
    modules = {name: str(i+2)*40 for i, name in enumerate(sources.MODULES)}
    files = {'source/' + n: b'fixture' for n in
             ('CMakeLists.txt', 'LICENSE.txt', 'LICENSING.md', 'vcpkg.json')}
    files.update({f'source/external/{n}/source.c': b'fixture' for n in modules})
    files['manifest.json'] = json_bytes(sources.manifest(files, revision, modules))
    return pack(files), modules


def pack(files):
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w') as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return output.getvalue()


class SourceTests(unittest.TestCase):
    def test_exact_revision_and_hashes(self):
        blob, modules = fixture()
        self.assertEqual(sources.verify(blob, '1'*40, modules)['files'], 7)
        with self.assertRaises(ValueError):
            sources.verify(blob, 'a'*40, modules)
        for name in sources.members(blob):
            with self.subTest(name=name):
                for remove in (False, True):
                    files = sources.members(blob)
                    if remove:
                        del files[name]
                    else:
                        files[name] += b'tamper'
                    with self.assertRaises(ValueError):
                        sources.verify(pack(files), '1'*40, modules)

    def test_unsafe_paths_and_extra_references(self):
        blob, modules = fixture()
        for path in ('../escape', 'C:/escape', 'source/_codex_refs/op25/file', 'source/CON'):
            files = sources.members(blob)
            files[path] = b'unexpected'
            with self.subTest(path=path), self.assertRaises(ValueError):
                sources.verify(pack(files), '1'*40, modules)

    def test_duplicate_and_link(self):
        for mode in ('duplicate', 'link'):
            output = io.BytesIO()
            with zipfile.ZipFile(output, 'w') as archive:
                archive.writestr('file', b'a')
                entry = zipfile.ZipInfo('FILE' if mode == 'duplicate' else 'link')
                if mode == 'link':
                    entry.external_attr = 0o120777 << 16
                archive.writestr(entry, b'file')
            with self.assertRaises(ValueError):
                sources.members(output.getvalue())

    def test_only_committed_files_and_dirty_refusal(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp).resolve()
            def git(*args):
                return subprocess.run(['git','-C',str(repo),*args], check=True,
                                      capture_output=True,text=True).stdout.strip()
            git('init')
            git('config','user.name','Source Test')
            git('config','user.email','test@example.invalid')
            (repo/'source.cpp').write_text('committed',encoding='ascii')
            git('add','source.cpp'); git('commit','-m','fixture')
            revision = git('rev-parse','HEAD')
            (repo/'private-key.txt').write_text('untracked-secret',encoding='ascii')
            self.assertEqual(sources.committed_archive(repo,revision), {'source.cpp':b'committed'})
            (repo/'source.cpp').write_text('modified',encoding='ascii')
            with self.assertRaisesRegex(ValueError,'Modified tracked'):
                sources.committed_archive(repo,revision)
            with self.assertRaises(ValueError):
                sources.committed_archive(repo,'0'*40)


if __name__ == '__main__':
    unittest.main()
