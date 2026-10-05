"""DEC-0196: finite, source-bound distribution review and runtime materials."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile
import zipfile

from package_inventory import json_bytes, parse_json, require, sha256
from project_sources import members, committed_archive
from stage_runtime import config, plain
from rds_toolchain import fetch, SHA as TOOLCHAIN_SHA, VERSION, URL as TOOLCHAIN_URL

KIT = 'licenses/distribution/materials.zip'
MAX_KIT = 64 * 1024**2
REVIEW_VERSION = 'DEC-0196-1'
MINGW_SHA = 'fa41d462041ed72e29a5101049e5991d4aea5c552fd75f7ac0074989fc54ceb7'
MINGW_URL = 'https://codeload.github.com/mingw-w64/mingw-w64/tar.gz/refs/tags/v12.0.0'
LIBM_SHA = 'b6d2cec3eae94f9f509c767b45932f1ada8350c4bdb85af2fcab4a3c14807981'
LICENSES = {
    'GPL-3.0.txt': '8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903',
    'GCC-Runtime-Exception-3.1.txt': '9d6b43ce4d8de0c878bf16b54d8e7a10d9bd42b75178153e3af6a815bdc90f74',
    'MinGW-w64-12.0.0-COPYING.txt': '99a69660981156c21336fdb5661f89341b013c94e4bf9e1c7467b4745718397f',
    'Winpthreads-12.0.0-COPYING.txt': '63263614cdd29f2f93cba85e992f041b31f9fc7b4033692f31269489a8a1b177',
}
EMBEDDED = {
    'external/miniaudio': ('MIT-0 OR Unlicense', 'LICENSE'),
    'external/mbelib': ('ISC', 'COPYRIGHT'),
    'external/liquid-dsp': ('MIT', 'LICENSE'),
    'external/redsea-block': ('MIT', 'LICENSE'),
    'external/ggmorse': ('MIT', 'LICENSE'),
    'external/sgp4': ('Unlicense', 'LICENSE'),
    'external/acars': ('MIT AND BSD-2-Clause', 'LICENSE.md'),
    'external/aero/jaero': ('MIT', 'LICENSE'),
    'external/aero/jfft': ('MIT', 'LICENSE'),
    'external/aero/correct': ('BSD-3-Clause', 'LICENSE'),
    'external/aero/codec': ('ISC AND MIT', 'COPYRIGHT'),
    'src/sstv_backend/vendor/sstv': ('MIT', 'LICENSE'),
    'resources/icao': ('CC0-1.0', 'LICENSE-CC0.txt'),
    'resources/maps': ('Public domain', 'README.md'),
}
PORT_LICENSES = {'fmt': 'MIT', 'jansson': ('MIT', 'MIT AND dtoa'), 'libsodium': 'ISC',
                 'libusb': 'LGPL-2.1-or-later', 'nlohmann-json': 'MIT',
                 'pthreads': 'Apache-2.0', 'rtlsdr': 'GPL-2.0-or-later',
                 'soapysdr': 'BSL-1.0', 'spdlog': 'MIT', 'zlib': 'Zlib'}
# These are reviewed attribution expressions, NOT a general licence classifier.
QT_LICENSES = {
    'MIT', 'BSD-2-Clause', 'BSD-3-Clause', 'Apache-2.0', 'CC0-1.0',
    'CC0-1.0 OR Apache-2.0', 'FTL OR GPL-2.0-only', 'Zlib', 'MIT AND MIT-open-group',
    'LicenseRef-ICC-License', 'IJG AND BSD-3-Clause', 'Libpng', 'Libpng AND libpng-2.0',
    'MPL-2.0', 'LicenseRef-BSD-3-Clause-with-PCRE2-Binary-Like-Packages-Exception',
    'LicenseRef-SHA1-Public-Domain', 'Bitstream-Vera', 'LicenseRef-Lcs-Telegraphics',
    'Unicode-DFS-2016', 'Unicode-3.0', 'BSD-2-Clause AND Imlib2', 'X11 AND HPND',
    'Apache-2.0 OR MIT', 'HPND-sell-variant', 'BSL-1.0',
    'urn:dje:license:bitstream',  # Qt 6.7's Vera source-only WebAssembly font.
}
QT_MODULES = {'qtcore', 'qtgui', 'qtnetwork', 'qtwidgets', 'qtsvg'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def qt_review(blob):
    rows = []
    with zipfile.ZipFile(io.BytesIO(blob)) as kit:
        for name in kit.namelist():
            if not name.endswith('.tar.xz'):
                continue
            with tarfile.open(fileobj=io.BytesIO(kit.read(name)), mode='r|xz') as archive:
                for entry in archive:
                    if not entry.isfile() or not entry.name.endswith('qt_attribution.json'):
                        continue
                    require(entry.size <= 1024**2, 'Oversized Qt attribution')
                    raw = archive.extractfile(entry).read()
                    # Upstream attribution syntax includes literal newlines in strings.
                    value = json.loads(raw.decode('utf-8'), strict=False)
                    for row in value if isinstance(value, list) else [value]:
                        runtime = (row.get('QDocModule') in QT_MODULES
                                   and not set(row.get('QtParts', [])) & {'tools', 'tests'})
                        if runtime:
                            require(row.get('LicenseId') in QT_LICENSES,
                                    f'Unreviewed Qt runtime licence: {row.get("Id")}: {row.get("LicenseId")}')
                        rows.append({'path': entry.name, 'id': row.get('Id'),
                                     'license': row.get('LicenseId'), 'runtimeCandidate': runtime,
                                     'selection': 'FTL' if row.get('LicenseId') == 'FTL OR GPL-2.0-only'
                                                  else row.get('LicenseId'), 'sha256': digest(raw)})
    require(any(r['id'] == 'xsvg' for r in rows), 'Qt SVG attribution missing')
    return rows


def source_review(blob):
    files = members(blob)
    rows = []
    for root, (licence, notice) in EMBEDDED.items():
        prefix = 'source/' + root + '/'
        require(prefix + notice in files, 'Missing reviewed source notice: ' + root)
        subset = {n: digest(b) for n, b in files.items() if n.startswith(prefix)}
        rows.append({'root': root, 'license': licence, 'noticeSha256': digest(files[prefix + notice]),
                     'files': len(subset), 'sourceTreeSha256': digest(json_bytes(subset))})
    known = tuple('source/' + root + '/' for root in EMBEDDED)
    for name in files:
        if name.startswith('source/external/') and name.endswith(('.c', '.cc', '.cpp', '.h', '.hpp', '.rs')):
            require(name.startswith(known), 'Unreviewed embedded code root: ' + name)
    return rows


def compiler_materials(doc, stage, archive):
    require(sha256(archive) == TOOLCHAIN_SHA, 'RDS compiler distribution not pinned')
    require(sha256(stage / 'sdrtown_rds_dsp.dll') ==
            sha256(plain(doc['builtFiles']['sdrtown_rds_dsp.dll'])),
            'Packaged RDS DLL differs from the qualified build output')
    cc = plain(doc['rdsCompiler'])
    root = cc.parent.parent
    cache = Path(doc['buildRoot']) / 'rds-dsp-runtime/CMakeCache.txt'
    text = cache.read_text(encoding='utf-8')
    require(str(cc).replace('\\', '/') in text.replace('\\', '/'), 'RDS build uses a different compiler')
    payload, hashes = {}, {}
    with zipfile.ZipFile(archive) as z:
        expected = z.read('mingw64/version_info.txt')
        require((root / 'version_info.txt').read_bytes() == expected, 'RDS toolchain version mismatch')
        payload['compiler/version_info.txt'] = expected
        selected = [n for n in z.namelist() if n.endswith(('.a', '.exe', '.dll'))]
        for name in selected:
            relative = name.removeprefix('mingw64/')
            wanted = digest(z.read(name))
            require(sha256(plain(root / relative)) == wanted, 'Compiler/runtime differs from pinned archive')
            hashes[relative] = wanted
        # Preserve the actual libstdc++ per-file notices, including HP exceptions.
        for name in z.namelist():
            if name.startswith(f'mingw64/include/c++/{VERSION}/') and not name.endswith('/'):
                data = z.read(name)
                if not name.endswith('.gch'):
                    payload['compiler/' + name.removeprefix('mingw64/')] = data
    link = Path(doc['buildRoot']) / 'rds-dsp-runtime/CMakeFiles/sdrtown_rds_dsp.dir/link.txt'
    command = link.read_text(encoding='utf-8')
    require(all(x in command for x in ('-static', '-static-libgcc', '-static-libstdc++')),
            'Unreviewed RDS linkage')
    require('-fplugin' not in command and '-flto' not in command, 'Unreviewed GCC intermediate-code processing')
    return payload, {'archiveSha256': TOOLCHAIN_SHA, 'archiveUrl': TOOLCHAIN_URL,
                     'gccVersion': VERSION, 'mingwVersion': '12.0.0', 'inputs': hashes,
                     'inputScope': 'Compiler binaries and archive candidates, not a claim all are linked',
                     'dllSha256': sha256(stage / 'sdrtown_rds_dsp.dll'),
                     'runtimeException': 'GCC-exception-3.1', 'eligibleSourceCompilation': True}


def review_ports(read, inputs):
    for port, licences in PORT_LICENSES.items():
        allowed = licences if isinstance(licences, tuple) else (licences,)
        require(inputs['vcpkg'][port]['license'] in allowed, 'Unreviewed dependency licence: ' + port)
    if inputs['vcpkg']['jansson']['license'] == 'MIT AND dtoa':
        notice = read('licenses/vcpkg/jansson/copyright')
        require(b'Lucent Technologies' in notice and b'provided that this entire notice' in notice,
                'Jansson dtoa notice missing')


def review(read, inputs):
    review_ports(read, inputs)
    return {'policy': REVIEW_VERSION, 'sourceCommit': inputs['sourceCommit'],
            'combinedApplicationLicense': 'GPL-3.0-or-later', 'originalSourceLicense': 'MIT',
            'embedded': source_review(read('licenses/project/source-materials.zip', 256 * 1024**2)),
            'qt': qt_review(read('licenses/qt/source-materials.zip', 96 * 1024**2)),
            'vcpkg': inputs['vcpkg']}


def export(doc, stage, repo, cache, toolchain_archive):
    stage = plain(stage, directory=True)
    inputs = parse_json((stage / 'licenses/build-inputs.json').read_bytes())
    record = review(lambda n, limit=0: (stage / n).read_bytes(), inputs)
    payload, compiler = compiler_materials(doc, stage, toolchain_archive)
    payload['compiler.json'] = json_bytes(compiler)
    payload['review.json'] = json_bytes(record)
    for name, wanted in LICENSES.items():
        raw = (repo / 'resources/licenses' / name).read_bytes()
        require(digest(raw) == wanted, 'Modified original licence text: ' + name)
        payload['notices/' + name] = raw
    payload['sources/mingw-w64-12.0.0.tar.gz'] = fetch(
        MINGW_URL, cache / 'mingw-w64-12.0.0.tar.gz', MINGW_SHA, 32 * 1024**2).read_bytes()
    payload['sources/libm-0.2.16.crate'] = fetch(
        'https://static.crates.io/crates/libm/libm-0.2.16.crate',
        cache / 'libm-0.2.16.crate', LIBM_SHA, 2 * 1024**2).read_bytes()
    for directory, revision in (('rtl-driver-source', '6ca357c15cbf676ff30eb8eb445d1e1eac17c136'),
                                ('sdrplay-driver-source', '48bd8b41072534018de1d74deb3dea5874d9e0e0')):
        for name, data in committed_archive(Path(doc['buildRoot']) / directory, revision).items():
            payload[f'drivers/{directory}/{name}'] = data
    payload['DISTRIBUTION.md'] = (repo / 'DISTRIBUTION.md').read_bytes()
    payload['manifest.json'] = json_bytes({'schema': 1, 'sourceCommit': inputs['sourceCommit'],
        'files': {n: digest(b) for n, b in sorted(payload.items())}})
    require(sum(map(len, payload.values())) < MAX_KIT, 'Distribution source budget exceeded')
    target = stage / KIT
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in sorted(payload.items()):
            z.writestr(name, data)
    for name in LICENSES:
        (target.parent / name).write_bytes(payload['notices/' + name])
    (stage / 'DISTRIBUTION.md').write_bytes(payload['DISTRIBUTION.md'])
    print('PASS distribution materials:', len(record['embedded']), 'embedded roots,',
          len(record['qt']), 'Qt attribution entries; pinned GCC/MinGW inputs')


def verify(blob, read, inputs, entries):
    require(len(blob) < MAX_KIT, 'Distribution archive budget exceeded')
    files = members(blob)
    require(sum(map(len, files.values())) < MAX_KIT, 'Distribution expansion limit')
    manifest = parse_json(files.pop('manifest.json', b'{}'))
    require(manifest == {'schema': 1, 'sourceCommit': inputs['sourceCommit'],
                        'files': {n: digest(b) for n, b in sorted(files.items())}}, 'Distribution manifest mismatch')
    require(parse_json(files['review.json']) == review(read, inputs), 'Distribution review/source mismatch')
    for name, wanted in LICENSES.items():
        require(digest(files['notices/' + name]) == wanted and
                read('licenses/distribution/' + name) == files['notices/' + name], 'Distribution notice mismatch')
    require(files['DISTRIBUTION.md'] == read('DISTRIBUTION.md'), 'Distribution terms mismatch')
    for name, wanted in (('mingw-w64-12.0.0.tar.gz', MINGW_SHA), ('libm-0.2.16.crate', LIBM_SHA)):
        require(digest(files['sources/' + name]) == wanted, 'Supplementary source mismatch')
    compiler = parse_json(files['compiler.json'])
    require(compiler['archiveSha256'] == TOOLCHAIN_SHA and compiler['gccVersion'] == VERSION and
            compiler['mingwVersion'] == '12.0.0' and compiler['eligibleSourceCompilation'] is True and
            compiler['dllSha256'] == entries['sdrtown_rds_dsp.dll']['sha256'], 'Compiler evidence mismatch')
    require(len(compiler['inputs']) > 20 and
            'compiler/include/c++/14.2.0/vector' in files, 'Missing compiler-runtime evidence')
    for directory in ('rtl-driver-source', 'sdrplay-driver-source'):
        require(f'drivers/{directory}/LICENSE.txt' in files and
                f'drivers/{directory}/CMakeLists.txt' in files, 'Missing driver sources')
    return {'review': REVIEW_VERSION, 'gccVersion': VERSION, 'mingwVersion': '12.0.0'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--cache', type=Path)
    parser.add_argument('--toolchain-archive', type=Path)
    parser.add_argument('--preflight', action='store_true', help='Review source/notice kits before compiling')
    args = parser.parse_args()
    if args.preflight:
        inputs = parse_json((args.stage / 'licenses/build-inputs.json').read_bytes())
        record = review(lambda n, limit=0: (args.stage / n).read_bytes(), inputs)
        print('PASS distribution preflight:', len(record['embedded']), 'embedded roots,',
              len(record['qt']), 'Qt attributions, all dependency notices')
    else:
        require(args.cache is not None and args.toolchain_archive is not None,
                'Full material export requires --cache and --toolchain-archive')
        export(config(args.config), args.stage, args.repo, args.cache, args.toolchain_archive)
