"""DEC-0186: committed application/build sources, with pinned external submodules."""
import argparse
import hashlib
import io
from pathlib import Path
import re
import stat
import subprocess
import tempfile
import zipfile

from package_inventory import git, json_bytes, parse_json, require, safe_name
from stage_runtime import destination, plain

KIT = 'licenses/project/source-materials.zip'
MAX_BYTES = 256 * 1024 * 1024
MAX_FILES = 20000
MODULES = ('liquid-dsp', 'mbelib', 'miniaudio')
SCOPE = ('Committed application and external build dependencies; reference-only '
         '_codex_refs submodules excluded. Qt/vcpkg/toolchain materials are separate.')


def members(blob):
    require(len(blob) <= MAX_BYTES, 'Project source ZIP too large')
    result, names = {}, set()
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        infos = archive.infolist()
        require(len(infos) <= MAX_FILES and sum(i.file_size for i in infos) <= MAX_BYTES,
                'Project source resource limit exceeded')
        for info in infos:
            require(info.orig_filename == info.filename, 'Normalized source name')
            name = safe_name(info.filename.rstrip('/') if info.is_dir() else info.filename)
            require(name.lower() not in names, 'Duplicate project source member')
            names.add(name.lower())
            kind = stat.S_IFMT(info.external_attr >> 16)
            require(not info.flag_bits & 1 and kind in (0, stat.S_IFREG, stat.S_IFDIR),
                    'Linked/encrypted project source member')
            if not info.is_dir():
                require(kind != stat.S_IFDIR, 'Invalid project source type')
                result[name] = archive.read(info)
    return result


def manifest(files, revision, submodules):
    require(re.fullmatch(r'[0-9a-f]{40}', revision) and set(submodules) == set(MODULES)
            and all(re.fullmatch(r'[0-9a-f]{40}', v) for v in submodules.values()),
            'Invalid project source revisions')
    return {'schema': 1, 'sourceCommit': revision, 'submodules': submodules, 'scope': SCOPE,
            'files': {n: {'size': len(b), 'sha256': hashlib.sha256(b).hexdigest()}
                      for n, b in sorted(files.items())}}


def verify(blob, revision, submodules):
    files = members(blob)
    record = parse_json(files.pop('manifest.json', b'{}'))
    require(all(n.startswith('source/') for n in files), 'Unexpected project source prefix')
    require({'source/CMakeLists.txt', 'source/LICENSE.txt', 'source/LICENSING.md',
             'source/vcpkg.json'} <= files.keys(), 'Incomplete application sources')
    require(not any(n.startswith('source/_codex_refs/') for n in files),
            'Reference-only clones cannot enter source kit')
    for module in MODULES:
        require(any(n.startswith(f'source/external/{module}/') for n in files),
                'Missing external source module')
    require(record == manifest(files, revision, submodules), 'Project source manifest mismatch')
    return {'files': len(files), 'bytes': sum(len(b) for b in files.values())}


def committed_archive(repo, revision):
    require(git(repo, 'rev-parse', 'HEAD') == revision, 'Source checkout revision mismatch')
    require(not git(repo, 'status', '--porcelain', '--untracked-files=no'),
            'Modified tracked source inputs')
    with tempfile.TemporaryDirectory() as temp:
        output = Path(temp) / 'source.zip'
        subprocess.run(['git', '-C', str(repo), 'archive', '--format=zip',
                        '--output=' + str(output), revision], check=True, timeout=120)
        require(output.stat().st_size <= MAX_BYTES, 'Source archive too large')
        return members(output.read_bytes())


def collect(repo, revision, submodules):
    repo = plain(repo, directory=True)
    files = {'source/' + n: b for n, b in committed_archive(repo, revision).items()
             if not n.startswith('_codex_refs/')}
    for module in MODULES:
        prefix = 'external/' + module
        entry = git(repo, 'ls-tree', revision, '--', prefix).split()
        require(len(entry) >= 3 and entry[0] == '160000' and entry[2] == submodules[module],
                'Recorded submodule revision mismatch')
        child = plain(repo / prefix, directory=True)
        for name, data in committed_archive(child, submodules[module]).items():
            path = 'source/' + prefix + '/' + name
            require(path not in files, 'Colliding embedded source')
            files[path] = data
    require(len(files) < MAX_FILES and sum(map(len, files.values())) <= MAX_BYTES,
            'Project source resource limit exceeded')
    files['manifest.json'] = json_bytes(manifest(files, revision, submodules))
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    blob = output.getvalue()
    verify(blob, revision, submodules)
    return blob


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', required=True, type=Path)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--stage', required=True, type=Path)
    args = parser.parse_args()
    stage = destination(args.build, args.stage)
    inputs = parse_json((stage / 'licenses/build-inputs.json').read_bytes())
    blob = collect(args.repo, inputs['sourceCommit'], inputs['submodules'])
    path = stage / KIT
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(blob)
    print('PASS project source kit:', verify(blob, inputs['sourceCommit'], inputs['submodules']))


if __name__ == '__main__':
    main()
