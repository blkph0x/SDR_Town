"""DEC-0188: tracked dependency-build scripts plus receipt-verified CMake helpers."""
import argparse
import hashlib
import io
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

from package_inventory import json_bytes, parse_json, require
from project_sources import members
from stage_runtime import config, destination, plain

KIT = 'licenses/vcpkg/tooling-materials.zip'
MAX_BYTES = 64 * 1024 * 1024
MAX_FILES = 4096
ROOTS = ('scripts', 'triplets', 'bootstrap-vcpkg.bat', 'bootstrap-vcpkg.sh',
         '.vcpkg-root', 'LICENSE.txt', 'NOTICE.txt', 'README.md',
         'ports/vcpkg-cmake', 'ports/vcpkg-cmake-config', 'ports/vcpkg-msbuild',
         'ports/vcpkg-cmake-get-vars', 'ports/vcpkg-pkgconfig-get-modules',
         'ports/vcpkg-tool-meson', 'ports/pkgconf')
HELPERS = ('vcpkg-cmake', 'vcpkg-cmake-config', 'vcpkg-msbuild',
           'vcpkg-cmake-get-vars', 'vcpkg-pkgconfig-get-modules', 'vcpkg-tool-meson')
SCOPE = ('Tracked vcpkg tooling and installed CMake helpers. Bootstrap may download '
         'tools; compiler/SDK and independent runtime rebuild remain separate prerequisites.')


def git(root, *args):
    # Administrator-installed SDK directory; trust only this explicitly configured
    # checkout for this invocation, never change global Git safety settings.
    return ['git', '-c', 'safe.directory=' + root.as_posix(), '-C', str(root), *args]


def output(root, *args):
    return subprocess.check_output(git(root, *args), timeout=120).decode('utf-8').strip()


def manifest(files, source, revision):
    require(all(isinstance(v, str) and re.fullmatch('[0-9a-f]{40}', v)
                for v in (source, revision)), 'Invalid tooling revision')
    return {'schema': 1, 'sourceCommit': source, 'toolingCommit': revision, 'scope': SCOPE,
            'files': {n: {'sha256': hashlib.sha256(b).hexdigest(), 'size': len(b)}
                      for n, b in sorted(files.items())}}


def verify(blob, source):
    require(len(blob) <= MAX_BYTES, 'Tooling ZIP size limit')
    files = members(blob)
    record = parse_json(files.pop('manifest.json', b'{}'))
    require(len(files) <= MAX_FILES and sum(map(len, files.values())) <= MAX_BYTES,
            'Tooling resource limit')
    required = {'tooling/' + n for n in ROOTS if '.' in n and '/' not in n}
    required |= {'tooling/scripts/buildsystems/vcpkg.cmake', 'tooling/triplets/x64-windows.cmake',
                 'tooling/scripts/vcpkg-tool-metadata.txt',
                 'tooling/ports/pkgconf/vcpkg.json', 'tooling/ports/pkgconf/portfile.cmake'}
    require(required <= files.keys(), 'Incomplete dependency tooling')
    for name in files:
        require(any(name == 'tooling/' + n or name.startswith('tooling/' + n + '/') for n in ROOTS)
                or any(name.startswith('installed/' + p + '/') for p in HELPERS),
                'Unexpected tooling member')
    for helper in HELPERS:
        prefix = 'installed/' + helper + '/'
        require(prefix + 'copyright' in files, 'Missing helper notice')
        receipt = parse_json(files.get(prefix + 'vcpkg.spdx.json', b'{}'))
        expected = {'copyright', 'vcpkg.spdx.json', 'vcpkg_abi_info.txt'}
        for item in receipt.get('files', []):
            name = item.get('fileName', '')
            if item.get('SPDXID', '').startswith('SPDXRef-port-file-'):
                from vcpkg_sources import checksum
                path = 'tooling/ports/' + helper + '/' + name.removeprefix('./')
                require(path in files and hashlib.sha256(files[path]).hexdigest() == checksum(item, 'sha256'),
                        'Helper recipe does not match installed receipt')
            if not name.startswith('./share/' + helper + '/'):
                continue
            leaf = name[len('./share/' + helper + '/'):]
            from vcpkg_sources import checksum
            require(prefix + leaf in files and hashlib.sha256(files[prefix + leaf]).hexdigest() == checksum(item, 'sha256'),
                    'Helper receipt mismatch')
            expected.add(leaf)
        require(any(n.endswith('.cmake') for n in expected), 'Missing installed CMake helper')
        require({n[len(prefix):] for n in files if n.startswith(prefix)} == expected, 'Unexpected installed helper')
    require(record == manifest(files, source, record.get('toolingCommit')), 'Tooling manifest mismatch')
    return {'files': len(files), 'bytes': sum(map(len, files.values())), 'toolingCommit': record['toolingCommit']}


def collect(root, installed, source):
    root, installed = plain(root, directory=True), plain(installed, directory=True)
    revision = output(root, 'rev-parse', 'HEAD')
    require(not output(root, 'status', '--porcelain', '--untracked-files=no', '--', *ROOTS),
            'Modified dependency tooling inputs')
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / 'tooling.zip'
        subprocess.run(git(root, 'archive', '--format=zip', '--output=' + str(path), revision, *ROOTS),
                       check=True, timeout=120)
        require(path.stat().st_size <= MAX_BYTES, 'Tooling ZIP size limit')
        files = {'tooling/' + n: b for n, b in members(path.read_bytes()).items()}
    for helper in HELPERS:
        base = plain(installed / 'share' / helper, directory=True)
        for path in base.rglob('*'):
            plain(path, directory=path.is_dir())
            if path.is_file():
                require(path.stat().st_size <= MAX_BYTES, 'Helper file too large')
                files['installed/' + helper + '/' + path.relative_to(base).as_posix()] = path.read_bytes()
    files['manifest.json'] = json_bytes(manifest(files, source, revision))
    result = io.BytesIO()
    with zipfile.ZipFile(result, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    blob = result.getvalue()
    verify(blob, source)
    return blob


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    args = parser.parse_args()
    doc = config(args.config)
    stage = destination(doc['buildRoot'], args.stage)
    source = parse_json((stage / 'licenses/build-inputs.json').read_bytes())['sourceCommit']
    blob = collect(Path(doc['vcpkgRoot']), Path(doc['vcpkgBin']).parent, source)
    path = stage / KIT
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    try:
        temporary.write_bytes(blob)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)
    print('PASS dependency tooling:', verify(blob, source))


if __name__ == '__main__':
    main()
