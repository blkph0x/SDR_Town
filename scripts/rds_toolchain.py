"""DEC-0196: one hash-pinned RDS compiler distribution for release provenance."""
import argparse
import hashlib
import os
from pathlib import Path
import stat
import tempfile
import urllib.request
import zipfile

from package_inventory import require, safe_name, sha256

VERSION = '14.2.0'
MINGW_VERSION = '12.0.0'
TAG = '14.2.0posix-18.1.8-12.0.0-ucrt-r1'
ARCHIVE = 'winlibs-x86_64-posix-seh-gcc-14.2.0-mingw-w64ucrt-12.0.0-r1.zip'
URL = f'https://github.com/brechtsanders/winlibs_mingw/releases/download/{TAG}/{ARCHIVE}'
SHA = '403380c3c125b5ba565d6b29d1f4aa9e18e6080f048da97bee841979d520b4c4'


def fetch(url, target, expected, maximum):
    target = Path(target)
    if target.is_file():
        require(target.stat().st_size <= maximum and sha256(target) == expected,
                'Cached material checksum mismatch: ' + target.name)
        return target
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            with urllib.request.urlopen(url, timeout=120) as response:
                require(response.url.startswith('https://'), 'Insecure material redirect')
                size = 0
                while block := response.read(1024 * 1024):
                    size += len(block)
                    require(size <= maximum, 'Material size limit exceeded')
                    stream.write(block)
            stream.close()
            require(sha256(temporary) == expected, 'Downloaded material checksum mismatch')
            os.replace(temporary, target)
        finally:
            stream.close()
            temporary.unlink(missing_ok=True)
    return target


def install(root, archive):
    require(sha256(archive) == SHA, 'Unqualified RDS toolchain archive')
    root.mkdir(parents=True, exist_ok=True)
    root = root.resolve()
    with zipfile.ZipFile(archive) as z:
        infos = z.infolist()
        require(len(infos) < 40000 and sum(i.file_size for i in infos) < 2 * 1024**3,
                'Toolchain expansion limit')
        names = set()
        for info in infos:
            name = safe_name(info.filename.rstrip('/'))
            require(name.startswith('mingw64/') or name == 'mingw64', 'Toolchain root mismatch')
            require(name.lower() not in names, 'Duplicate toolchain entry')
            names.add(name.lower())
            require(stat.S_IFMT(info.external_attr >> 16) in (0, stat.S_IFREG, stat.S_IFDIR),
                    'Linked toolchain entry')
            target = root / name
            require(target.resolve().is_relative_to(root), 'Toolchain extraction escape')
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                data = z.read(info)
                if target.exists():
                    require(target.is_file() and sha256(target) == hashlib.sha256(data).hexdigest(),
                            'Existing compiler differs; use a new empty directory')
                else:
                    with target.open('xb') as stream:
                        stream.write(data)
    print(root / 'mingw64/bin/gcc.exe')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    archive = args.archive or fetch(URL, args.root / ARCHIVE, SHA, 300 * 1024**2)
    install(args.root, archive)
