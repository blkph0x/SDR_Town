"""Fail closed on mismatched release assets; no private signing key is needed."""
import argparse
import base64
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
import zipfile


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify(root, version, installer):
    manifest_path = root / 'update.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    require(manifest['version'] == version and manifest['tag'] == f'v{version}', 'Manifest version mismatch')
    require(manifest['installer']['size'] == installer.stat().st_size, 'Installer size mismatch')
    require(manifest['installer']['sha256'] == digest(installer), 'Installer hash mismatch')
    require(manifest['installer']['url'] ==
            f'https://github.com/Blkph0x/SDR_Town/releases/download/v{version}/SDR_Town-{version}-win64-setup.exe',
            'Unexpected installer URL')
    portable = root / f'SDR_Town-{version}-win64-portable.zip'
    control = root / f'SdrTownControl-{version}-win64.dll'
    sums = {}
    for line in (root / 'SHA256SUMS.txt').read_text().splitlines():
        if line.strip():
            sha, name = line.split(maxsplit=1)
            require(name not in sums, 'Duplicate checksum filename')
            sums[name] = sha
    for path in (installer, portable, control):
        require(sums.get(path.name) == digest(path), f'Checksum mismatch: {path.name}')
    with zipfile.ZipFile(portable) as archive:
        names = archive.namelist()
        require(len(names) == len(set(n.lower() for n in names)), 'Duplicate ZIP path')
        for name in names:
            path = PurePosixPath(name)
            require(not path.is_absolute() and '..' not in path.parts and ':' not in name and '\\' not in name,
                    f'Unsafe ZIP path: {name}')
            require(path.suffix.lower() not in ('.pem', '.key', '.pdb', '.log', '.wav', '.iq', '.cf32'),
                    f'Private/debug/capture artifact in ZIP: {name}')
        required = ('SDR_Town.exe', 'SdrTownControl.dll', 'sdrtown_rds_dsp.dll', 'rtlsdr.dll',
                    'SoapySDR.dll', 'SoapyRTLSDR.dll', 'Qt6Core.dll', 'Qt6Widgets.dll',
                    'platforms/qwindows.dll', 'licenses/rtlsdr-COPYRIGHT.txt',
                    'sdrtown_sstv.exe', 'licenses/sstv/sstv-MIT.txt',
                    'licenses/sstv/libm-LICENSE.txt', 'licenses/sstv/rust-COPYRIGHT-library.html')
        for name in required:
            require(name in names, f'Missing runtime: {name}')
            require(archive.read(name) == (root / 'build/deploy_staging' / name).read_bytes(),
                    f'Staging mismatch: {name}')
        require(archive.read('SdrTownControl.dll') == control.read_bytes(), 'Standalone control DLL mismatch')
        require(archive.read('rtlsdr.dll') ==
                (root / 'build/vcpkg_installed/x64-windows/bin/rtlsdr.dll').read_bytes(),
                'RTL runtime differs from configured dependency')
    key_text = (root / 'resources/update_manifest_ed25519_pub.inc').read_text()
    match = re.search(r'"([0-9a-fA-F]{64})"', key_text)
    require(match is not None, 'Embedded signing key missing')
    signature = base64.b64decode((root / 'update.json.sig').read_text().strip(), validate=True)
    require(len(signature) == 64, 'Invalid signature size')
    openssl = shutil.which('openssl') or r'C:\Program Files\Git\usr\bin\openssl.exe'
    with tempfile.TemporaryDirectory(prefix='sdr-release-verify-') as temp:
        public = Path(temp) / 'public.der'
        sig = Path(temp) / 'signature.bin'
        # RFC 8410 Ed25519 SubjectPublicKeyInfo, with the application's 32-byte key.
        public.write_bytes(bytes.fromhex('302a300506032b6570032100' + match[1]))
        sig.write_bytes(signature)
        subprocess.run([openssl, 'pkeyutl', '-verify', '-pubin', '-keyform', 'DER',
                        '-inkey', str(public), '-rawin', '-in', str(manifest_path),
                        '-sigfile', str(sig)], check=True, timeout=30)
    print(f'PASS release {version}: signed manifest, asset hashes, portable runtime and control DLL')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--version', required=True)
    parser.add_argument('--installer', type=Path, required=True)
    args = parser.parse_args()
    verify(Path(__file__).resolve().parents[1], args.version, args.installer.resolve())
