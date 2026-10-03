"""DEC-0178: pinned, version-specific Qt sources; never executes archive content."""
import argparse
import hashlib
import io
from pathlib import Path, PurePosixPath
import re
import tarfile
import tempfile
import urllib.request
import zipfile

from package_inventory import (MAX_JSON, json_bytes, parse_json, require, safe_name, sha256)
from stage_runtime import config, plain

KIT = 'licenses/qt/source-materials.zip'
MAX_KIT = 96 * 1024 * 1024
MAX_ARCHIVE = 64 * 1024 * 1024
MAX_EXPANDED = 512 * 1024 * 1024
MODULES = ('qtbase', 'qtsvg', 'qttools')
# From the official archive's adjacent .sha256 files; changes require review.
PINS = {
    '6.7.3': (
        '8ccbb9ab055205ac76632c9eeddd1ed6fc66936fc56afc2ed0fd5d9e23da3097',
        '40142cb71fb1e07ad612bc361b67f5d54cd9367f9979ae6b86124a064deda06b',
        'f03bb7df619cd9ac9dba110e30b7bcab5dd88eb8bdc9cc752563b4367233203f'),
    '6.11.1': (
        'd9594a31228aa23ad6b531719a29b45f0f3989fe6c136d45767ea179f233c1ac',
        '7f3cf02f4824bf03c2c5859ea6db173bf1482a1daf24e6cdf7bc78cfa26a8a94',
        '8e61835a679c93fa9c6065b142353c2071ba68e297898937c32a03777fcaf50d'),
}
CONFIG_FILES = ('mkspecs/qconfig.pri', 'include/QtCore/qconfig.h',
                'include/QtCore/qtcore-config.h', 'include/QtGui/qtgui-config.h',
                'include/QtWidgets/qtwidgets-config.h', 'include/QtNetwork/qtnetwork-config.h')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pins(version):
    require(version in PINS, 'Qt version has no reviewed source pins')
    return dict(zip(MODULES, PINS[version]))


def archive_name(module, version):
    return f'{module}-everywhere-src-{version}.tar.xz'


def url(module, version):
    return (f'https://download.qt.io/archive/qt/{version.rsplit(".", 1)[0]}/'
            f'{version}/submodules/{archive_name(module, version)}')


def fetch(version, cache):
    """Explicit network action, HTTPS only; complete downloads installed atomically."""
    cache.mkdir(parents=True, exist_ok=True)
    cache = plain(cache, directory=True)
    for module, expected in pins(version).items():
        target = cache / archive_name(module, version)
        if target.exists():
            plain(target)
            require(target.stat().st_size <= MAX_ARCHIVE and sha256(target) == expected,
                    'Cached Qt archive checksum mismatch')
            continue
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=cache, delete=False) as out:
                temporary = Path(out.name)
                with urllib.request.urlopen(url(module, version), timeout=90) as response:
                    require(response.url.startswith('https://'), 'Insecure Qt download redirect')
                    total = 0
                    while block := response.read(1024 * 1024):
                        total += len(block)
                        require(total <= MAX_ARCHIVE, 'Qt archive size limit exceeded')
                        out.write(block)
            require(sha256(temporary) == expected, 'Downloaded Qt archive checksum mismatch')
            temporary.replace(target)
        finally:
            if temporary and temporary.exists():
                temporary.unlink()
        print(f'PASS: official Qt source {target.name}')


def catalog(data, module, version):
    """Readable notice index in addition to complete sources, not a linked-code SBOM."""
    require(len(data) <= MAX_ARCHIVE, 'Qt archive size limit exceeded')
    root = f'{module}-everywhere-src-{version}/'
    notices, names, expanded = [], set(), 0
    # Stream avoids unbounded TarInfo accumulation. No extraction to disk.
    with tarfile.open(fileobj=io.BytesIO(data), mode='r|xz') as archive:
        for entry in archive:
            name = entry.name.rstrip('/')
            require(name.startswith(root) or name == root[:-1], 'Unexpected Qt archive root')
            require(not name.startswith('/') and '\\' not in name and ':' not in name and
                    all(p not in ('', '.', '..') for p in name.split('/')), 'Unsafe Qt archive path')
            require(name not in names, 'Duplicate Qt archive path')
            names.add(name)
            expanded += entry.size
            require(len(names) <= 40000 and expanded <= MAX_EXPANDED, 'Qt archive resource limit exceeded')
            require(entry.isdir() or entry.isfile(), 'Non-regular Qt archive entry')
            relative = name[len(root):]
            leaf = PurePosixPath(name).name.lower()
            selected = (relative.startswith('LICENSES/') or leaf == 'qt_attribution.json' or
                        re.search(r'^(license|licence|copying|copyright|notice|authors)([._-]|$)', leaf) or
                        ('/3rdparty/' in name and leaf.endswith(('.txt', '.md', '.rst'))))
            if entry.isfile() and selected:
                require(entry.size <= MAX_JSON, 'Qt notice size limit exceeded')
                raw = archive.extractfile(entry).read(entry.size + 1)
                require(len(raw) == entry.size, 'Truncated Qt notice')
                notices.append((relative, raw))
    require(any(n.startswith('LICENSES/') for n, _ in notices), 'Qt license texts missing')
    text = ('Qt source notice catalog. Includes optional/test/build dependencies;\n'
            'not a claim that every listed component is linked in the runtime.\n'
            'Complete original sources accompany this catalog.\n\n').encode()
    for name, raw in sorted(notices):
        text += f'===== {module}/{name} (SHA256 {digest(raw)}) =====\n'.encode()
        # Escape undecodable bytes rather than silently dropping notice content.
        text += raw.decode('utf-8', errors='backslashreplace').encode('utf-8') + b'\n\n'
    require(len(text) <= MAX_JSON, 'Qt catalog size limit exceeded')
    return text, len(notices)


def instructions(version):
    return f'''# Qt {version}: corresponding sources and replacement

SDR Town dynamically links Qt. Its original code is MIT; Qt and its embedded
third-party components retain their own terms. See each module's LICENSES,
qt_attribution.json files and the accompanying notice catalogs. This kit does
not select a commercial Qt license or certify combined-distribution compliance.

sources/ contains unmodified official qtbase, qtsvg and qttools archives,
version {version}, with pinned SHA256 receipts in manifest.json. qtbase covers
Core, Gui, Widgets, Network and their plugins; qtsvg covers Svg and SVG plugins.
qttools supplies the deployment tool source. sdk/ records this SDK's selected
configuration headers, not a complete recreation of Qt's upstream build farm.

To rebuild: unpack the modules into a short new path, use an x64 Visual Studio
developer environment, CMake, Ninja and Python supported by that Qt release.
Configure qtbase with -opensource -confirm-license -release -shared
-nomake examples -nomake tests -prefix C:/qt-rebuilt/{version}. Only confirm
the license after reviewing it. Build and install with cmake --build and
cmake --install. Configure qtsvg and qttools using the installed
bin/qt-configure-module.bat, then build and install them. Review sdk/ feature
settings and Qt's configure output when reproducing optional dependencies.
These are source build instructions, not a bit-for-bit build claim.

Replacement: close SDR Town, make a separate copy of its portable directory,
then deploy the rebuilt matching x64/shared Release Qt DLLs AND their matching
plugins to that copy using the rebuilt windeployqt. Do not mix debug/release,
architectures or plugin builds. Keep the originals for rollback. No signature
or hash enforcement in SDR Town prohibits user replacement. Run CLI help and
GUI dry-run tests before receiving. For a narrow integration smoke test, build
only qtsvg against the existing exact-version Qt SDK and replace Qt6Svg.dll,
imageformats/qsvg.dll and iconengines/qsvgicon.dll in the disposable copy.
Passing that smoke test does not prove a complete independent Qtbase rebuild.

Official obligations: https://www.qt.io/development/open-source-lgpl-obligations
Official sources: https://download.qt.io/archive/qt/
'''.encode()


def export(doc, stage, cache):
    version = doc['qtVersion']
    sdk = plain(Path(doc['qtBin']).parent, directory=True)
    stage = plain(stage, directory=True)
    from package_inventory import tree_files
    tree_files(stage)
    files, archives, counts = {}, {}, {}
    for module, expected in pins(version).items():
        source = plain(cache / archive_name(module, version))
        require(source.stat().st_size <= MAX_ARCHIVE and sha256(source) == expected,
                'Qt source checksum mismatch')
        data = source.read_bytes()
        name = 'sources/' + source.name
        files[name] = data
        files[f'notices/{module}.txt'], counts[module] = catalog(data, module, version)
        archives[module] = {'name': name, 'sha256': expected, 'url': url(module, version)}
    for name in CONFIG_FILES:
        p = plain(sdk / name)
        require(p.stat().st_size <= MAX_JSON, 'Qt SDK config size limit exceeded')
        files['sdk/' + name] = p.read_bytes()
    qconfig = files['sdk/mkspecs/qconfig.pri'].decode('utf-8-sig')
    require(re.search(r'^QT_VERSION\s*=\s*' + re.escape(version) + r'\s*$', qconfig, re.M),
            'Configured Qt SDK version mismatch')
    files['REBUILD.md'] = instructions(version)
    files['manifest.json'] = json_bytes({'schema': 1, 'qtVersion': version, 'archives': archives,
        'noticeCounts': counts, 'files': {n: digest(d) for n, d in sorted(files.items())},
        'scope': 'Exact upstream sources and selected SDK settings; full rebuild not yet certified'})
    target = stage / KIT
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as out:
            temporary = Path(out.name)
            with zipfile.ZipFile(out, 'w', zipfile.ZIP_STORED) as archive:
                for name, data in sorted(files.items()):
                    info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                    info.external_attr = 0o100644 << 16
                    archive.writestr(info, data)
        require(temporary.stat().st_size <= MAX_KIT, 'Qt kit size limit exceeded')
        verify(temporary.read_bytes(), version)
        temporary.replace(target)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()
    print(f'PASS: Qt {version}, three source archives, {sum(counts.values())} notice entries')


def verify(data, version):
    require(len(data) <= MAX_KIT, 'Qt kit size limit exceeded')
    expected_pins = pins(version)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        infos = archive.infolist()
        require(len(infos) <= 32 and sum(i.file_size for i in infos) <= MAX_KIT,
                'Qt kit resource limit exceeded')
        names = set()
        for entry in infos:
            safe_name(entry.filename)
            require(entry.orig_filename == entry.filename and entry.filename not in names and
                    entry.compress_type == zipfile.ZIP_STORED and not (entry.flag_bits & 1) and
                    (entry.external_attr >> 16) == 0o100644, 'Invalid Qt kit entry')
            names.add(entry.filename)
        expected_names = {'manifest.json', 'REBUILD.md', *('sdk/' + n for n in CONFIG_FILES),
                          *(f'notices/{m}.txt' for m in MODULES),
                          *('sources/' + archive_name(m, version) for m in MODULES)}
        require(names == expected_names, 'Qt kit file inventory mismatch')
        require(archive.getinfo('manifest.json').file_size <= MAX_JSON, 'Qt manifest size limit exceeded')
        doc = parse_json(archive.read('manifest.json'))
        require(doc.get('schema') == 1 and doc.get('qtVersion') == version, 'Qt kit version mismatch')
        hashes, counts, sources = {}, {}, {}
        for name in sorted(names - {'manifest.json'}):
            maximum = MAX_ARCHIVE if name.startswith('sources/') else MAX_JSON
            require(archive.getinfo(name).file_size <= maximum, 'Qt entry size limit exceeded')
            hashes[name] = digest(archive.read(name))
        require(doc.get('files') == hashes, 'Qt kit file checksum mismatch')
        for module, expected in expected_pins.items():
            name = 'sources/' + archive_name(module, version)
            require(hashes[name] == expected, 'Qt source checksum mismatch')
            notice, counts[module] = catalog(archive.read(name), module, version)
            require(archive.read(f'notices/{module}.txt') == notice, 'Qt notice catalog mismatch')
            sources[module] = {'name': name, 'sha256': expected, 'url': url(module, version)}
        require(doc.get('archives') == sources and doc.get('noticeCounts') == counts,
                'Qt source receipt mismatch')
        require(archive.read('REBUILD.md') == instructions(version), 'Qt rebuild instructions mismatch')
        require(re.search(rb'^QT_VERSION\s*=\s*' + version.encode().replace(b'.', rb'\.') + rb'\s*$',
                          archive.read('sdk/mkspecs/qconfig.pri'), re.M), 'Qt SDK version mismatch')
    return {'qtVersion': version, 'archiveCount': 3, 'noticeCount': sum(counts.values()),
            'kitSha256': digest(data), 'kitBytes': len(data), 'fullRebuildVerified': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--fetch', action='store_true')
    args = parser.parse_args()
    try:
        doc = config(args.config)
        if args.fetch:
            fetch(doc['qtVersion'], args.cache)
        if args.stage:
            export(doc, args.stage, args.cache)
        require(args.fetch or args.stage, 'Choose fetch or stage')
    except (ValueError, OSError, KeyError, TypeError, tarfile.TarError, zipfile.BadZipFile) as exc:
        parser.exit(1, f'Qt sources failed: {exc}\n')
