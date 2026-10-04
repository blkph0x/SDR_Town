"""DEC-0172: exact-file packaging evidence, not a legal clearance or full SBOM."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import zipfile


INVENTORY = 'package-inventory.json'
INPUTS = 'licenses/build-inputs.json'
RUNTIME_INPUTS = 'licenses/runtime-deployment.json'
SOURCE_KIT = 'licenses/vcpkg/source-materials.zip'
POLICY = 'T-0104-notices-7'
MAX_FILES = 10000
MAX_BYTES = 512 * 1024 * 1024
MAX_JSON = 8 * 1024 * 1024
ROOT_NOTICES = ('LICENSE.txt', 'LICENSING.md', 'ACKNOWLEDGEMENTS.md')
PORTS = ('fmt', 'jansson', 'libsodium', 'libusb', 'nlohmann-json',
         'pthreads', 'rtlsdr', 'soapysdr', 'spdlog', 'zlib')
SOURCE_NOTICES = {
    'external/miniaudio/LICENSE': 'licenses/miniaudio/LICENSE',
    'external/mbelib/COPYRIGHT': 'licenses/mbelib/COPYRIGHT',
    'external/mbelib/README.md': 'licenses/mbelib/README.md',
    'external/aero/codec/README.md': 'licenses/aero/codec-README.md',
}
VCPKG_DLLS = {
    'fmt.dll': 'fmt', 'jansson.dll': 'jansson', 'libsodium.dll': 'libsodium',
    'libusb-1.0.dll': 'libusb', 'rtlsdr.dll': 'rtlsdr', 'soapysdr.dll': 'soapysdr',
    'spdlog.dll': 'spdlog', 'z.dll': 'zlib', 'pthreadvc3.dll': 'pthreads',
    'pthreadvce3.dll': 'pthreads', 'pthreadvse3.dll': 'pthreads',
}
QT_FILES = {
    'qt6core.dll', 'qt6gui.dll', 'qt6widgets.dll', 'qt6network.dll', 'qt6svg.dll',
    'platforms/qwindows.dll', 'styles/qmodernwindowsstyle.dll',
    'styles/qwindowsvistastyle.dll', 'generic/qtuiotouchplugin.dll',
    'iconengines/qsvgicon.dll', 'imageformats/qgif.dll', 'imageformats/qico.dll',
    'imageformats/qjpeg.dll', 'imageformats/qsvg.dll',
    'networkinformation/qnetworklistmanager.dll', 'tls/qcertonlybackend.dll',
    'tls/qopensslbackend.dll', 'tls/qschannelbackend.dll',
}
MSVC_FILES = {
    'concrt140.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
    'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll', 'vccorlib140.dll',
    'vcruntime140.dll', 'vcruntime140_1.dll', 'vcruntime140_threads.dll',
    'vc_redist.x64.exe',
}
PROJECT_FILES = {
    'sdr_town.exe', 'sdrtowncontrol.dll', 'build-info.json', 'qt.conf',
    'remote_diagnostics.defaults.json', 'remote_diagnostics.json',
    'tools/diagnostics/remote_diag_server.py', 'scripts/start_remote_diag_server.ps1',
    'scripts/install_remote_diag_task.ps1',
    *(name.lower() for name in ROOT_NOTICES),
}
STATIC_NOTICES = (
    *SOURCE_NOTICES.values(), 'licenses/ggmorse/LICENSE', 'licenses/sgp4/LICENSE',
    'licenses/acars/LICENSE.md', 'licenses/acars/JANSSON-LICENSE.txt',
    'licenses/acars/ZLIB-LICENSE.txt', 'licenses/aero/JAERO-MIT.txt',
    'licenses/aero/JFFT-MIT.txt', 'licenses/aero/libcorrect-BSD.txt',
    'licenses/aero/codec-COPYRIGHT.txt', 'licenses/aero/libaeroambe-MIT.txt',
    'licenses/aero/NaturalEarth.txt',
)
KNOWN_NOTICES = {
    *STATIC_NOTICES, INPUTS, RUNTIME_INPUTS, SOURCE_KIT,
    'licenses/qt/source-materials.zip', 'licenses/msvc/runtime-materials.json',
    'licenses/project/source-materials.zip',
    'licenses/vcpkg/tooling-materials.zip',
    'licenses/msvc/license.rtf', 'licenses/msvc/README.txt',
    'licenses/embedded-inputs.json', 'licenses/acars/ASN1-NOTICES.txt',
    'licenses/icao/LICENSE-CC0.txt', 'licenses/icao/README.md',
    'licenses/SoapyRTLSDR-LICENSE.txt', 'licenses/SoapySDRPlay3-LICENSE.txt',
    'licenses/rtlsdr-COPYRIGHT.txt', 'licenses/liquid-dsp-LICENSE.txt',
    'licenses/redsea-block/LICENSE', 'licenses/redsea-block/UPSTREAM.md',
    'licenses/sgp4/README.sdr-town.md', 'licenses/ggmorse/README.sdr-town.md',
    'licenses/aero/README.sdr-town.md', 'licenses/acars/README.sdr-town.md',
    'licenses/acars/UPSTREAM-README.md', 'licenses/sstv/README.md',
    'licenses/sstv/sstv-MIT.txt', 'licenses/sstv/libm-LICENSE.txt',
    'licenses/sstv/rust-COPYRIGHT-library.html',
    *(f'licenses/vcpkg/{p}/{n}' for p in PORTS for n in ('copyright', 'vcpkg.spdx.json')),
    *(f'licenses/redsea-block/src/{n}' for n in (
        'block_sync.cc', 'block_sync.hh', 'constants.hh', 'group.cc', 'group.hh', 'options.hh',
        'dsp/liquid_wrappers.cc', 'dsp/liquid_wrappers.hh', 'dsp/subcarrier.cc', 'dsp/subcarrier.hh',
        'io/bitbuffer.hh', 'io/mpx_buffer.hh', 'util/maybe.hh', 'util/util.hh')),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + '\n').encode('utf-8')


def parse_json(data):
    require(len(data) <= MAX_JSON, 'JSON size limit exceeded')

    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'Duplicate JSON key')
            result[key] = value
        return result

    def invalid_constant(value):
        raise ValueError('Non-finite JSON constant')

    return json.loads(data.decode('utf-8-sig'), object_pairs_hook=unique, parse_constant=invalid_constant)


def safe_name(name):
    path = PurePosixPath(name)
    require(name and not path.is_absolute() and '\\' not in name and ':' not in name,
            f'Unsafe package path: {name}')
    require(all(part not in ('', '.', '..') and part[-1:] not in (' ', '.')
                and not re.match(r'^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)', part, re.I)
                and not any(ord(c) < 32 for c in part) for part in name.split('/')),
            f'Unsafe package path: {name}')
    return name


def linked(path):
    attrs = getattr(path.lstat(), 'st_file_attributes', 0)
    return path.is_symlink() or bool(attrs & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0x400))


def tree_files(root):
    require(root.is_dir() and not linked(root), 'Expected a non-linked staging directory')
    result, names, total = {}, set(), 0
    for parent, dirs, files in os.walk(root, followlinks=False):
        for base in dirs + files:
            path = Path(parent) / base
            require(not linked(path), 'Linked/reparse package entry is forbidden')
            name = safe_name(path.relative_to(root).as_posix())
            require(name.lower() not in names, f'Case-colliding package path: {name}')
            names.add(name.lower())
            require(len(names) <= MAX_FILES, 'Package resource limit exceeded')
            if base in files:
                require(path.is_file(), 'Non-regular package entry')
                total += path.stat().st_size
                require(total <= MAX_BYTES, 'Package resource limit exceeded')
                result[name] = path
    require(len(names) <= MAX_FILES and total <= MAX_BYTES, 'Package resource limit exceeded')
    return result


def component(name):
    lower = name.lower()
    if lower in PROJECT_FILES:
        return 'sdr-town'
    if lower in VCPKG_DLLS:
        return VCPKG_DLLS[lower]
    if lower in QT_FILES:
        return 'qt'
    if lower in MSVC_FILES:
        return 'msvc-runtime'
    if lower == 'soapyrtlsdr.dll':
        return 'soapy-rtlsdr'
    if lower == 'sdrplaysupport.dll':
        return 'soapy-sdrplay'
    if lower == 'sdr_aero_codec.dll':
        return 'aero-codec'
    if lower == 'sdrtown_rds_dsp.dll':
        return 'rds-mingw'
    if lower == 'sdrtown_sstv.exe':
        return 'sstv-rust'
    if re.fullmatch(r'data/inmarsat/(3f5|4f2|4f3|6f1|i4a)\.json', lower) or lower == 'data/inmarsat/readme.md':
        return 'inmarsat-channel-data'
    if name in KNOWN_NOTICES:
        return 'notice-or-source-evidence'
    raise ValueError(f'Uninventoried package file: {name}')


def required_notices(names):
    from embedded_notices import COPIES, EVIDENCE, NOTICES
    required = {*ROOT_NOTICES, INPUTS, SOURCE_KIT, 'licenses/project/source-materials.zip', *STATIC_NOTICES,
                EVIDENCE, NOTICES, *COPIES.values()}
    for port in PORTS:
        required.update((f'licenses/vcpkg/{port}/copyright', f'licenses/vcpkg/{port}/vcpkg.spdx.json'))
    by_component = {
        'soapy-rtlsdr': ('licenses/SoapyRTLSDR-LICENSE.txt',),
        'soapy-sdrplay': ('licenses/SoapySDRPlay3-LICENSE.txt',),
        'rds-mingw': ('licenses/redsea-block/LICENSE', 'licenses/liquid-dsp-LICENSE.txt'),
        'sstv-rust': ('licenses/sstv/sstv-MIT.txt', 'licenses/sstv/libm-LICENSE.txt',
                      'licenses/sstv/rust-COPYRIGHT-library.html'),
    }
    for name in names:
        required.update(by_component.get(component(name), ()))
    if any(component(n) in ('qt', 'msvc-runtime') for n in names):
        required.add(RUNTIME_INPUTS)
    if any(component(n) == 'qt' for n in names):
        required.add('licenses/qt/source-materials.zip')
    if any(component(n) == 'msvc-runtime' for n in names):
        required.update(('licenses/msvc/runtime-materials.json', 'licenses/msvc/license.rtf',
                         'licenses/msvc/README.txt', 'vc_redist.x64.exe'))
    return required


def blockers(names):
    """No generated 'approved' flag can bypass outstanding reviewed requirements."""
    components = {component(n) for n in names}
    result = ['ISS-0060: transitive static/source/data notice inventory and combined-distribution review incomplete']
    if 'qt' in components:
        result.append('ISS-0060: Qt linked third-party distribution review incomplete (replacement rebuild passed)')
    if 'rtlsdr' in components:
        result.append('ISS-0060: RTL-SDR/libusb/pthreads full tooling/rebuild and distribution review incomplete')
    if 'rds-mingw' in components:
        result.append('ISS-0060: static MinGW runtime version, notices and exception evidence incomplete')
    if 'msvc-runtime' in components:
        result.append('ISS-0060: Microsoft publisher redistribution entitlement review incomplete')
    return result


def git(repo, *args):
    return subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True,
                          text=True, timeout=30).stdout.strip()


def port_info(data, name):
    doc = parse_json(data)
    require(isinstance(doc, dict) and isinstance(doc.get('packages'), list) and
            all(isinstance(p, dict) for p in doc['packages']), 'Invalid SPDX package schema')
    matches = [p for p in doc.get('packages', []) if p.get('SPDXID') == 'SPDXRef-port' and p.get('name') == name]
    require(len(matches) == 1 and matches[0].get('versionInfo'), f'Invalid SPDX identity: {name}')
    p = matches[0]
    return {'version': p['versionInfo'], 'license': p.get('licenseConcluded', 'NOASSERTION')}


def stage_notices(repo, stage, vcpkg, qt_version):
    from embedded_notices import collect
    tree_files(stage)  # Reject links before writing inside a caller-supplied tree.
    require(re.fullmatch(r'\d+\.\d+\.\d+', qt_version), 'Expected actual configured Qt version')
    revision = git(repo, 'rev-parse', 'HEAD')
    embedded = collect(repo, revision)  # Validate before changing the prior notices.
    copies = {name: repo / name for name in ROOT_NOTICES}
    copies.update({dest: repo / src for src, dest in SOURCE_NOTICES.items()})
    ports = {}
    for port in PORTS:
        for leaf in ('copyright', 'vcpkg.spdx.json'):
            copies[f'licenses/vcpkg/{port}/{leaf}'] = vcpkg / 'share' / port / leaf
        ports[port] = port_info((vcpkg / 'share' / port / 'vcpkg.spdx.json').read_bytes(), port)
    for dest, source in copies.items():
        require(source.is_file() and source.stat().st_size > 0, f'Missing notice input: {dest}')
        target = stage / dest
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    origins = {}
    for file in (vcpkg / 'bin').glob('*.dll'):
        if file.name.lower() in VCPKG_DLLS:
            origins[file.name.lower()] = sha256(file)
    submodules = {}
    for name in ('miniaudio', 'mbelib', 'liquid-dsp'):
        path = 'external/' + name
        recorded = git(repo, 'ls-tree', 'HEAD', '--', path).split()
        actual = git(repo / path, 'rev-parse', 'HEAD')
        require(len(recorded) >= 3 and recorded[0] == '160000' and actual == recorded[2],
                f'Submodule differs from recorded input: {name}')
        require(not git(repo / path, 'status', '--porcelain', '--untracked-files=no'),
                f'Dirty dependency input: {name}')
        submodules[name] = actual
    evidence = {'schema': 1, 'sourceCommit': revision, 'qtVersion': qt_version,
                'vcpkg': ports, 'vcpkgBinarySha256': origins, 'submodules': submodules,
                'scope': 'Configured build inputs; not a complete transitive SBOM or source kit'}
    (stage / INPUTS).write_bytes(json_bytes(evidence))
    for name, data in embedded.items():
        target = stage / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


def make_document(entries, read):
    # Import at verification time: the source exporter reuses the bounded JSON
    # and package path rules above; ordinary runtime staging does not need it.
    from vcpkg_sources import MAX_KIT, checksum, verify_kit
    from embedded_notices import EVIDENCE, NOTICES, verify as verify_embedded
    for name in required_notices(entries):
        require(name in entries and entries[name]['size'] > 0, f'Missing/empty required notice: {name}')
    require('SDR_Town.exe' in entries and 'build-info.json' in entries, 'Missing application/provenance')
    if 'rtlsdr.dll' in entries:
        require('libusb-1.0.dll' in entries, 'RTL runtime missing required libusb-1.0.dll')
    inputs = parse_json(read(INPUTS))
    info = parse_json(read('build-info.json'))
    require(inputs.get('schema') == 1, 'Invalid build-input schema')
    require(re.fullmatch(r'[0-9a-f]{40}', inputs.get('sourceCommit', '')) is not None,
            'Invalid input source commit')
    require(info.get('sourceCommit') == inputs['sourceCommit'], 'Build-input source commit mismatch')
    require(info.get('executableSha256') == entries['SDR_Town.exe']['sha256'], 'Executable provenance mismatch')
    require(re.fullmatch(r'\d+\.\d+\.\d+', inputs.get('qtVersion', '')) is not None, 'Invalid Qt input version')
    receipts = {}
    for port in PORTS:
        receipts[port] = (read(f'licenses/vcpkg/{port}/vcpkg.spdx.json'),
                          read(f'licenses/vcpkg/{port}/copyright'))
        actual = port_info(receipts[port][0], port)
        require(inputs.get('vcpkg', {}).get(port) == actual, f'Dependency metadata mismatch: {port}')
    for name, entry in entries.items():
        if name.lower() in VCPKG_DLLS:
            require(inputs.get('vcpkgBinarySha256', {}).get(name.lower()) == entry['sha256'],
                    f'Runtime differs from installed build dependency: {name}')
            doc = parse_json(receipts[VCPKG_DLLS[name.lower()]][0])
            binary = [f for f in doc.get('files', [])
                      if f.get('SPDXID', '').startswith('SPDXRef-binary-file-') and
                      f.get('fileName', '').lower() == './bin/' + name.lower()]
            require(len(binary) == 1 and checksum(binary[0], 'sha256') == entry['sha256'],
                    f'Runtime differs from source receipt: {name}')
    deployed = {n: e['sha256'] for n, e in entries.items() if e['component'] in ('qt', 'msvc-runtime')}
    qt_materials, microsoft_materials = None, None
    if deployed:
        runtime = parse_json(read(RUNTIME_INPUTS))
        require(runtime.get('schema') == 1 and runtime.get('qtVersion') == inputs['qtVersion'],
                'Runtime deployment Qt identity mismatch')
        require(re.fullmatch(r'[0-9a-f]{64}', runtime.get('windeployqtSha256', '')) is not None and
                re.fullmatch(r'\d+\.\d+\.\d+', runtime.get('msvcRedistVersion', '')) is not None,
                'Invalid runtime deployment identity')
        require(runtime.get('runtimeSha256') == deployed, 'Runtime deployment inventory mismatch: hashes')
        if any(e['component'] == 'qt' for e in entries.values()):
            from qt_sources import KIT as QT_KIT, MAX_KIT as QT_MAX, verify as verify_qt
            qt_materials = verify_qt(read(QT_KIT, QT_MAX), inputs['qtVersion'])
        if any(e['component'] == 'msvc-runtime' for e in entries.values()):
            from msvc_materials import verify as verify_msvc
            microsoft_materials = verify_msvc(read, entries, runtime)
    sources = verify_kit(read(SOURCE_KIT, MAX_KIT), receipts, inputs['sourceCommit'])
    import project_sources
    sources['project'] = project_sources.verify(
        read(project_sources.KIT, project_sources.MAX_BYTES), inputs['sourceCommit'], inputs['submodules'])
    import vcpkg_tooling
    sources['tooling'] = vcpkg_tooling.verify(
        read(vcpkg_tooling.KIT, vcpkg_tooling.MAX_BYTES), inputs['sourceCommit'])
    embedded = verify_embedded(read(EVIDENCE), read(NOTICES), inputs['sourceCommit'], entries)
    return {'schema': 1, 'policy': POLICY, 'sourceCommit': inputs['sourceCommit'],
            'scope': 'Exact files and known build inputs; NOT full transitive license clearance',
            'releaseBlockers': blockers(entries), 'sourceMaterials': sources,
            'embeddedMaterials': embedded, 'qtMaterials': qt_materials,
            'microsoftMaterials': microsoft_materials, 'files': entries}


def generate(stage):
    files = tree_files(stage)
    files.pop(INVENTORY, None)
    entries = {n: {'sha256': sha256(p), 'size': p.stat().st_size, 'component': component(n)}
               for n, p in sorted(files.items())}
    def bounded_read(name, maximum=MAX_JSON):
        require(files[name].stat().st_size <= maximum, 'Metadata size limit exceeded')
        return files[name].read_bytes()

    doc = make_document(entries, bounded_read)
    (stage / INVENTORY).write_bytes(json_bytes(doc))
    return doc


def verify_zip(path, require_publishable=False):
    require(path.stat().st_size <= MAX_BYTES, 'ZIP resource limit exceeded')
    with zipfile.ZipFile(path) as archive:
        infos = archive.infolist()
        require(len(infos) <= MAX_FILES and sum(i.file_size for i in infos) <= MAX_BYTES,
                'ZIP resource limit exceeded')
        names, files = set(), {}
        for info in infos:
            # Python's Windows ZIP reader normalizes backslashes and truncates
            # NULs in filename. Validate the original central-directory name.
            require(info.orig_filename == info.filename, 'Unsafe normalized ZIP path')
            name = safe_name(info.filename[:-1] if info.is_dir() else info.filename)
            require(name.lower() not in names, f'Duplicate ZIP path: {name}')
            names.add(name.lower())
            mode = stat.S_IFMT(info.external_attr >> 16)
            require(mode in (0, stat.S_IFREG, stat.S_IFDIR), f'Non-regular ZIP entry: {name}')
            require(not (info.flag_bits & 1), 'Encrypted ZIP entry forbidden')
            if not info.is_dir():
                require(mode != stat.S_IFDIR, 'Invalid ZIP entry mode')
                files[name] = info
        file_names_lower = {n.lower() for n in files}
        for name in files:
            require(not any(str(p).lower() in file_names_lower
                            for p in PurePosixPath(name).parents if str(p) != '.'),
                    'ZIP file/directory collision')
        require(INVENTORY in files, 'Missing package inventory')
        require(files[INVENTORY].file_size <= MAX_JSON, 'Inventory size limit exceeded')
        manifest = parse_json(archive.read(INVENTORY))
        entries = {}
        for name, info in files.items():
            if name == INVENTORY:
                continue
            h = hashlib.sha256()
            with archive.open(info) as source:
                for block in iter(lambda: source.read(1024 * 1024), b''):
                    h.update(block)
            entries[name] = {'sha256': h.hexdigest(), 'size': info.file_size, 'component': component(name)}

        def bounded_read(name, maximum=MAX_JSON):
            require(files[name].file_size <= maximum, 'Metadata size limit exceeded')
            return archive.read(name)

        actual = make_document(entries, bounded_read)
        require(manifest == actual, 'Package inventory mismatch')
        if require_publishable:
            require(not actual['releaseBlockers'], 'Publication blocked: ' + '; '.join(actual['releaseBlockers']))
        return actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    stage = sub.add_parser('stage-notices')
    stage.add_argument('--repo', type=Path, required=True)
    stage.add_argument('--stage', type=Path, required=True)
    stage.add_argument('--vcpkg', type=Path, required=True)
    stage.add_argument('--qt-version', required=True)
    generate_parser = sub.add_parser('generate')
    generate_parser.add_argument('--stage', type=Path, required=True)
    generate_parser.add_argument('--require-publishable', action='store_true')
    verify_parser = sub.add_parser('verify')
    verify_parser.add_argument('--zip', type=Path, required=True)
    verify_parser.add_argument('--require-publishable', action='store_true')
    args = parser.parse_args()
    try:
        if args.command == 'stage-notices':
            stage_notices(args.repo, args.stage, args.vcpkg, args.qt_version)
            print('PASS: project/dependency notices and configured input evidence staged')
            return
        doc = generate(args.stage) if args.command == 'generate' else verify_zip(args.zip, args.require_publishable)
        if args.require_publishable:
            require(not doc['releaseBlockers'], 'Publication blocked: ' + '; '.join(doc['releaseBlockers']))
        print(f"PASS: {len(doc['files'])} inventoried files; {len(doc['releaseBlockers'])} publication blockers")
        for blocker in doc['releaseBlockers']:
            print(blocker)
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError, zipfile.BadZipFile) as exc:
        parser.exit(1, f'Package gate failed: {exc}\n')


if __name__ == '__main__':
    main()
