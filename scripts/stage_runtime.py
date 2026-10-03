"""DEC-0174: stage declared build inputs without sweeping the developer bin tree."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

from package_inventory import (MAX_JSON, MSVC_FILES, VCPKG_DLLS, component, json_bytes,
                               linked, parse_json, require, safe_name, sha256, tree_files)


def plain(path, directory=False):
    path = Path(path).absolute()
    require(path.is_dir() if directory else path.is_file(), f'Missing runtime input: {path.name}')
    for part in (path, *path.parents):
        require(not linked(part), f'Linked runtime path: {path.name}')
    return path.resolve(strict=True)


def destination(build, stage):
    build = plain(build, directory=True)
    stage = Path(stage).absolute()
    require(stage == build / 'deploy_staging', 'Destination must be exactly build/deploy_staging')
    if stage.exists() or stage.is_symlink():
        tree_files(stage)  # Validate all descendants before any cleanup or writes.
    return stage


def config(path):
    path = plain(path)
    require(path.stat().st_size <= MAX_JSON, 'Runtime configuration size limit exceeded')
    doc = parse_json(path.read_bytes())
    require(doc.get('schema') == 1 and doc.get('configuration') == 'Release',
            'Runtime staging requires a Release configuration')
    return doc


def inputs(doc):
    result, names = {}, set()

    def add(name, source):
        safe_name(name)
        require('/' not in name and name.lower() not in names, 'Colliding runtime destination')
        component(name)  # Unknown binaries cannot enter by editing generated configuration.
        names.add(name.lower())
        result[name] = plain(source)

    for name, source in doc['builtFiles'].items():
        add(name, source)
    require({'SDR_Town.exe', 'SdrTownControl.dll', 'sdr_aero_codec.dll',
             'SoapyRTLSDR.dll', 'sdrPlaySupport.dll'} <= result.keys(), 'Required build outputs missing')
    vcpkg = plain(doc['vcpkgBin'], directory=True)
    # Imported UNKNOWN/INTERFACE targets and dynamically loaded SDR plugins are
    # not fully covered by TARGET_RUNTIME_DLLS. Use the configured dependency bin.
    for source in sorted(vcpkg.iterdir()):
        if source.name.lower() in VCPKG_DLLS:
            add(source.name, source)
    require({'rtlsdr.dll', 'libusb-1.0.dll', 'soapysdr.dll', 'jansson.dll',
             'z.dll', 'libsodium.dll', 'fmt.dll', 'spdlog.dll'} <= names,
            'Required configured dependency missing')
    for name, source in doc['moduleNotices'].items():
        safe_name(name)
        require(name in ('licenses/SoapyRTLSDR-LICENSE.txt', 'licenses/SoapySDRPlay3-LICENSE.txt'),
                'Unexpected module notice')
        result[name] = plain(source)
    require(len(doc['moduleNotices']) == 2, 'Missing module notices')
    return result


def prepare(doc, stage, clean=False):
    stage = destination(doc['buildRoot'], stage)
    copies = inputs(doc)  # Validate every input before touching the previous stage.
    for source in copies.values():
        require(not source.is_relative_to(stage), 'Input must not be inside staging')
    if clean:
        if stage.exists():
            shutil.rmtree(stage)
        stage.mkdir()
        return
    require(stage.is_dir(), 'Prepare staging before copying runtime inputs')
    for name, source in copies.items():
        target = stage / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    print(f'PASS: {len(copies)} declared runtime/notice inputs; no developer-directory sweep')


def version_key(path):
    try:
        return tuple(int(part) for part in path.name.split('.'))
    except ValueError:
        return ()


def deploy_qt(doc, stage):
    from msvc_materials import collect, EVIDENCE
    from qt_sources import export
    stage = destination(doc['buildRoot'], stage)
    require(stage.is_dir(), 'Missing staging directory')
    exe = plain(stage / 'SDR_Town.exe')
    qtbin = plain(doc['qtBin'], directory=True)
    tool = plain(qtbin / 'windeployqt.exe')
    compiler = plain(doc['compiler'])
    vc = next((p for p in compiler.parents if p.name == 'VC'), None)
    require(vc is not None, 'Expected configured MSVC compiler path')
    redists = sorted((vc / 'Redist/MSVC').iterdir(), key=version_key, reverse=True)
    redist = next((p for p in redists if p.is_dir() and version_key(p)), None)
    require(redist is not None, 'No configured MSVC redistributable')
    crts = list((redist / 'x64').glob('Microsoft.VC*.CRT'))
    require(len(crts) == 1, 'Expected one x64 MSVC CRT directory')
    crt = plain(crts[0], directory=True)
    runtime = {p.name: plain(p) for p in crt.iterdir() if p.suffix.lower() == '.dll'}
    require({'msvcp140.dll', 'vcruntime140.dll'} <= runtime.keys(), 'Incomplete MSVC runtime')
    require(all(n.lower() in MSVC_FILES for n in runtime), 'Unreviewed MSVC runtime')
    installer, materials = collect(redist, runtime)
    export(doc, stage, Path(doc['buildRoot']) / 'release-materials/qt')
    env = dict(os.environ, PATH=str(qtbin) + os.pathsep + os.environ.get('PATH', ''),
               VCINSTALLDIR=str(vc) + os.sep)
    # Same configured-tool flags on CI and local releases. Never run in bin/Release.
    subprocess.run([str(tool), '--release', '--force', '--dir', str(stage),
                    '--no-translations', '--no-system-d3d-compiler', '--no-opengl-sw',
                    '--no-compiler-runtime', str(exe)], env=env, check=True, timeout=180)
    for name, source in runtime.items():
        shutil.copyfile(source, stage / name)
    shutil.copyfile(installer, stage / installer.name)
    for name, data in materials.items():
        target = stage / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    files = tree_files(stage)
    files.pop('package-inventory.json', None)  # Regenerated after deployment.
    for name in files:
        component(name)
    require('platforms/qwindows.dll' in files and 'Qt6Core.dll' in files, 'Incomplete Qt deployment')
    # Evidence contains no host paths or credentials. The independent source-kit
    # gate remains open; runtime file hashes alone do not satisfy it.
    evidence = {'schema': 1, 'qtVersion': doc['qtVersion'],
                'windeployqtSha256': sha256(tool), 'msvcRedistVersion': redist.name,
                'msvcRuntimeVersion': parse_json(materials[EVIDENCE])['runtimeVersion'],
                'runtimeSha256': {n: sha256(p) for n, p in sorted(files.items())
                                  if component(n) in ('qt', 'msvc-runtime')}}
    evidence_path = stage / 'licenses/runtime-deployment.json'
    evidence_path.parent.mkdir(parents=True, exist_ok=True)
    evidence_path.write_bytes(json_bytes(evidence))
    print(f'PASS: configured Qt {doc["qtVersion"]}; MSVC {evidence["msvcRuntimeVersion"]}; runtime evidence staged')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'copy', 'qt'))
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    args = parser.parse_args()
    try:
        doc = config(args.config)
        if args.action == 'qt':
            deploy_qt(doc, args.stage)
        else:
            prepare(doc, args.stage, clean=args.action == 'prepare')
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as exc:
        parser.exit(1, f'Runtime staging failed: {exc}\n')
