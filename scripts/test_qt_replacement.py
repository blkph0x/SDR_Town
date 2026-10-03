"""Rebuild pinned QtSvg and exercise replacements without changing SDK/app files."""
import argparse
import io
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

from package_inventory import component, json_bytes, require, sha256, tree_files
from qt_sources import KIT, archive_name, verify
from stage_runtime import config, plain


def run(command, log, env=None, timeout=900):
    with log.open('wb') as output:
        subprocess.run([str(x) for x in command], stdout=output, stderr=subprocess.STDOUT,
                       check=True, timeout=timeout, env=env)


def test(doc, stage, output, repo):
    stage, repo = plain(stage, directory=True), plain(repo, directory=True)
    output.mkdir(parents=True, exist_ok=True)
    output = plain(output, directory=True)
    require(not output.is_relative_to(stage), 'Replacement output must not be inside package')
    files = tree_files(stage)
    evidence = verify(files[KIT].read_bytes(), doc['qtVersion'])
    before = {n: sha256(p) for n, p in files.items()}
    qt = plain(Path(doc['qtBin']).parent, directory=True)
    vc = next(p for p in plain(doc['compiler']).parents if p.name == 'VC')
    setup = plain(vc / 'Auxiliary/Build/vcvars64.bat')
    require(not any(c in str(setup) for c in '&|<>%^"'), 'Unsafe compiler environment path')
    # Use the SDK-supported Ninja generator with this configured VS environment.
    setup_result = subprocess.run(f'cmd.exe /d /u /s /c ""{setup}" >nul && set"',
                                  check=True, capture_output=True, timeout=60)
    build_env = dict(os.environ)
    for line in setup_result.stdout.decode('utf-16-le').splitlines():
        key, separator, value = line.partition('=')
        if separator and key:
            build_env[key.upper()] = value
    candidates = [shutil.which('ninja', path=build_env.get('PATH', build_env.get('Path'))),
                  qt.parent.parent / 'Tools/Ninja/ninja.exe',
                  vc.parent / 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe']
    ninja = next((plain(p) for p in candidates if p and Path(p).is_file()), None)
    require(ninja is not None, 'Ninja required for the Qt source replacement test')
    temporary = tempfile.TemporaryDirectory(prefix='qt-replacement-', dir=output)
    root = Path(temporary.name).resolve()
    # Validate the exact absolute cleanup target before TemporaryDirectory owns it.
    require(root.parent == output and root.name.startswith('qt-replacement-'), 'Unsafe temporary build path')
    try:
        with zipfile.ZipFile(files[KIT]) as kit:
            data = kit.read('sources/' + archive_name('qtsvg', doc['qtVersion']))
        # verify() has checked the pin, all members, sizes and no symlinks.
        source = root / 'source'
        source.mkdir()
        with tarfile.open(fileobj=io.BytesIO(data), mode='r:xz') as archive:
            for entry in archive:
                if not entry.isfile():
                    continue
                relative = Path(*entry.name.split('/')[1:])
                target = source / relative
                require(target.resolve().is_relative_to(source), 'Unsafe Qt source extraction')
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(entry) as inp, target.open('wb') as out:
                    shutil.copyfileobj(inp, out)
        build, install, portable = root / 'build', root / 'install', root / 'portable'
        run(['cmake', '-S', source, '-B', build, '-G', 'Ninja', f'-DCMAKE_MAKE_PROGRAM={ninja}',
             f'-DCMAKE_C_COMPILER={doc["compiler"]}', f'-DCMAKE_CXX_COMPILER={doc["compiler"]}',
             f'-DCMAKE_PREFIX_PATH={qt}', f'-DCMAKE_INSTALL_PREFIX={install}',
             '-DQT_BUILD_INTERNALS_NO_FORCE_SET_INSTALL_PREFIX=ON', '-DCMAKE_BUILD_TYPE=Release',
             '-DQT_BUILD_TESTS=OFF', '-DQT_BUILD_EXAMPLES=OFF'], output / 'configure.log', build_env)
        run(['cmake', '--build', build, '--config', 'Release', '-j', '4'], output / 'build.log', build_env)
        run(['cmake', '--install', build, '--config', 'Release'], output / 'install.log', build_env)
        for name, path in files.items():
            if name == 'package-inventory.json':
                continue
            if component(name) in ('qt', 'msvc-runtime', 'sdr-town', 'aero-codec', 'rds-mingw',
                                   'fmt', 'jansson', 'libsodium', 'libusb', 'rtlsdr', 'soapysdr', 'spdlog', 'zlib'):
                target = portable / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
        replacements = {'Qt6Svg.dll': install / 'bin/Qt6Svg.dll',
                        'imageformats/qsvg.dll': install / 'plugins/imageformats/qsvg.dll',
                        'iconengines/qsvgicon.dll': install / 'plugins/iconengines/qsvgicon.dll'}
        rebuilt = {}
        for name, path in replacements.items():
            plain(path)
            shutil.copyfile(path, portable / name)
            rebuilt[name] = sha256(path)
        probe = root / 'probe'
        run(['cmake', '-S', repo / 'tests/qt_replacement_probe', '-B', probe,
             '-G', 'Visual Studio 17 2022', '-A', 'x64',
             f'-DCMAKE_PREFIX_PATH={install};{qt}',
             f'-DQt6Svg_DIR={install}/lib/cmake/Qt6Svg'], output / 'probe-configure.log')
        run(['cmake', '--build', probe, '--config', 'Release', '-j', '4'], output / 'probe-build.log')
        shutil.copyfile(probe / 'Release/qt_replacement_probe.exe', portable / 'qt_replacement_probe.exe')
        # The portable package intentionally ships qwindows, not qoffscreen.
        # No window is shown by the probe; rasterization runs on QImages.
        env = dict(os.environ, QT_QPA_PLATFORM='windows')
        env.pop('QT_PLUGIN_PATH', None)
        env.pop('QT_QPA_PLATFORM_PLUGIN_PATH', None)
        run([portable / 'qt_replacement_probe.exe'], output / 'probe.log', env, timeout=30)
        run([portable / 'SDR_Town.exe', '--allow-multiple', '--no-control-server',
             '--no-remote-diagnostics', '--cli', '--cmd', 'help'], output / 'cli.log', env, timeout=45)
        require('biastee <device>' in (output / 'cli.log').read_text(errors='replace'),
                'Replaced Qt CLI did not reach command handling')
        require(before == {n: sha256(p) for n, p in tree_files(stage).items()},
                'Replacement test modified original package')
        (output / 'result.json').write_bytes(json_bytes({'schema': 1, 'qtVersion': doc['qtVersion'],
            'sourceKitSha256': evidence['kitSha256'], 'rebuiltSha256': rebuilt,
            'rendererAndPlugins': 'pass', 'applicationCli': 'pass',
            'originalPackageUnchanged': True, 'fullQtRebuildVerified': False}))
        print(f'PASS: rebuilt QtSvg {doc["qtVersion"]}; renderer/plugins and actual app CLI; original package unchanged')
    finally:
        temporary.cleanup()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    test(config(args.config), args.stage, args.output, Path(__file__).resolve().parent.parent)
