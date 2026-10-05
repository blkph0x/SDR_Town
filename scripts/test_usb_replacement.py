"""DEC-0196: rebuild the USB stack using only the exported source/tooling recipes."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile

from package_inventory import json_bytes, parse_json, require, sha256, tree_files, PORTS
from project_sources import members
from stage_runtime import config, plain
from test_qt_replacement import run
import vcpkg_sources
import vcpkg_tooling

RUNTIMES = ('rtlsdr.dll', 'libusb-1.0.dll', 'pthreadVC3.dll')


def test(doc, stage, output, work_root):
    stage = plain(stage, directory=True)
    output.mkdir(parents=True, exist_ok=True)
    output = plain(output, directory=True)
    require(not output.is_relative_to(stage), 'QA output cannot be inside package')
    source = stage / vcpkg_sources.KIT
    tooling = stage / vcpkg_tooling.KIT
    inputs = parse_json((stage / 'licenses/build-inputs.json').read_bytes())
    receipts = {p: ((stage / f'licenses/vcpkg/{p}/vcpkg.spdx.json').read_bytes(),
                    (stage / f'licenses/vcpkg/{p}/copyright').read_bytes()) for p in PORTS}
    vcpkg_sources.verify_kit(source.read_bytes(), receipts, inputs['sourceCommit'])
    vcpkg_tooling.verify(tooling.read_bytes(), inputs['sourceCommit'])
    before = {n: sha256(p) for n, p in tree_files(stage).items()}
    result = {'schema': 1, 'status': 'running', 'sourceCommit': inputs['sourceCommit'],
              'sourceKitSha256': sha256(source), 'toolingKitSha256': sha256(tooling),
              'originalRuntimeSha256': {n: before[n] for n in RUNTIMES},
              'binaryCache': 'disabled', 'network': 'build tools and hash-verified source downloads',
              'applicationSha256': before['SDR_Town.exe']}
    started = time.monotonic()
    work_root.mkdir(parents=True, exist_ok=True)
    work_root = plain(work_root, directory=True)
    require(not work_root.is_relative_to(stage), 'QA workspace cannot be inside package')
    try:
        with tempfile.TemporaryDirectory(prefix='usb-rebuild-', dir=work_root) as temporary:
            root = Path(temporary).resolve()
            require(root.parent == work_root and root.name.startswith('usb-rebuild-'), 'Unsafe QA workspace')
            vcpkg = root / 'vcpkg'
            vcpkg.mkdir()
            for name, data in members(tooling.read_bytes()).items():
                if name.startswith('tooling/'):
                    path = vcpkg / name.removeprefix('tooling/')
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
            for name, data in members(source.read_bytes()).items():
                if name.startswith('ports/'):
                    path = vcpkg / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
            env = {k.upper(): v for k, v in os.environ.items()}
            env.update(VCPKG_BINARY_SOURCES='clear', VCPKG_DISABLE_METRICS='1',
                       VCPKG_ROOT=str(vcpkg), VCPKG_FEATURE_FLAGS='-binarycaching')
            for key in ('VCPKG_INSTALLED_DIR', 'VCPKG_DEFAULT_BINARY_CACHE', 'VCPKG_DOWNLOADS',
                        'VCPKG_OVERLAY_PORTS', 'VCPKG_OVERLAY_TRIPLETS'):
                env.pop(key, None)
            # Git metadata is not required: all target/helper recipes are shipped overlays.
            run(['cmd.exe', '/d', '/c', str(vcpkg / 'bootstrap-vcpkg.bat'), '-disableMetrics'],
                output / 'bootstrap.log', env, 600)
            run([vcpkg / 'vcpkg.exe', 'install', 'rtlsdr:x64-windows', '--classic',
                 '--overlay-ports=' + str(vcpkg / 'ports'), '--binarysource=clear',
                 '--x-install-root=' + str(root / 'installed'), '--disable-metrics'],
                output / 'rebuild.log', env, 1800)
            portable = root / 'portable'
            shutil.copytree(stage, portable)
            replaced = {}
            for name in RUNTIMES:
                replacement = plain(root / 'installed/x64-windows/bin' / name)
                shutil.copyfile(replacement, portable / name)
                replaced[name] = sha256(replacement)
            # Verify build source identities, not just successful DLL load.
            for port in ('rtlsdr', 'libusb', 'pthreads'):
                rebuilt = (root / f'installed/x64-windows/share/{port}/vcpkg.spdx.json').read_bytes()
                from package_inventory import port_info
                require(port_info(rebuilt, port) == port_info(receipts[port][0], port),
                        'USB rebuilt version/license mismatch')
                original_doc, rebuilt_doc = map(parse_json, (receipts[port][0], rebuilt))
                def resources(record):
                    return sorted(vcpkg_sources.checksum(p, 'sha512') for p in record['packages']
                                  if p['SPDXID'].startswith('SPDXRef-resource-'))
                require(resources(original_doc) == resources(rebuilt_doc), 'USB source archive mismatch')
            run([sys.executable, Path(__file__).with_name('test_rtl_runtime_package.py'),
                 '--stage', portable], output / 'loader.log', env)
            run([portable / 'SDR_Town.exe', '--allow-multiple', '--cli', '--no-remote-diagnostics',
                 '--cmd', 'help'], output / 'cli.log', env, 60)
            result.update(rebuiltRuntimeSha256=replaced, loader='pass', cli='pass')
        require(before == {n: sha256(p) for n, p in tree_files(stage).items()}, 'QA modified package')
        result.update(status='pass', originalPackageUnchanged=True)
    except BaseException as exc:
        result.update(status='failed', error=f'{type(exc).__name__}: {exc}')
        raise
    finally:
        result['elapsedSeconds'] = round(time.monotonic() - started, 3)
        (output / 'result.json').write_bytes(json_bytes(result))
    print('PASS independent USB rebuild and isolated replacement loader')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--work-root', type=Path, required=True)
    args = parser.parse_args()
    test(config(args.config), args.stage, args.output, args.work_root)
