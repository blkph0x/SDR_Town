"""Load packaged RTL dependencies without PATH, existing handles or hardware I/O."""
import argparse
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from package_inventory import MSVC_FILES


def probe(stage):
    # LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32.
    # Fresh child process: neither user PATH nor the developer's Qt/vcpkg trees
    # can supply a missing dependency. Do not call any hardware enumeration API.
    rtl = ctypes.WinDLL(str(stage / 'rtlsdr.dll'), winmode=0x100 | 0x800)
    for name in ('rtlsdr_open', 'rtlsdr_close', 'rtlsdr_set_bias_tee'):
        getattr(rtl, name)
    ctypes.WinDLL(str(stage / 'SoapyRTLSDR.dll'), winmode=0x100 | 0x800)


def run_probe(stage):
    return subprocess.run([sys.executable, __file__, '--stage', str(stage), '--probe'],
                          capture_output=True, text=True, timeout=30)


def verify(stage, negative=True):
    required = ('rtlsdr.dll', 'libusb-1.0.dll', 'SoapySDR.dll', 'SoapyRTLSDR.dll')
    for name in required:
        if not (stage / name).is_file():
            raise ValueError(f'Missing packaged RTL dependency: {name}')
    result = run_probe(stage)
    if result.returncode:
        raise ValueError('Packaged RTL loader failed: ' + result.stderr.strip())
    if negative:
        with tempfile.TemporaryDirectory(prefix='sdr-rtl-package-') as temp:
            root = Path(temp).resolve()
            good, bad = root / 'complete', root / 'missing-usb'
            good.mkdir()
            bad.mkdir()
            for name in (*required, *MSVC_FILES):
                source = stage / name
                if source.is_file():
                    shutil.copyfile(source, good / name)
                    if name != 'libusb-1.0.dll':
                        shutil.copyfile(source, bad / name)
            clean = run_probe(good)
            if clean.returncode:
                raise ValueError('Isolated complete fixture failed: ' + clean.stderr.strip())
            missing = run_probe(bad)
            if missing.returncode != 20:
                raise ValueError('Missing-USB negative fixture did not fail in the loader')
    print('PASS: packaged RTL/module loads; missing USB rejected; no hardware I/O')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--probe', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.exit(1, 'This runtime loader gate requires Windows\n')
    stage = args.stage.resolve()
    if args.probe:
        try:
            probe(stage)
        except OSError as exc:
            parser.exit(20, f'OS loader failure: {exc}\n')
    else:
        try:
            verify(stage)
        except (ValueError, OSError, subprocess.SubprocessError) as exc:
            parser.exit(1, str(exc) + '\n')
