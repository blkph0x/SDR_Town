"""DEC-0175: exact source archives/patches, not a complete rebuild or license kit."""
import argparse
import hashlib
import io
import os
from pathlib import Path, PurePosixPath
import re
import stat
import tempfile
import zipfile

from package_inventory import (MAX_JSON, PORTS, json_bytes, parse_json, port_info,
                               require, safe_name)
from stage_runtime import config, destination, plain

KIT = 'licenses/vcpkg/source-materials.zip'
# Build-tool resource budgets, not limits imposed on receiver streams.
MAX_KIT = 128 * 1024 * 1024
MAX_MEMBERS = 512
MAX_CACHE_FILES = 4096
MAX_CACHE_BYTES = 512 * 1024 * 1024
SCOPE = 'Exact vcpkg sources and recipes; compiler/tooling/rebuild/other components still required'


def digest(data, algorithm='sha256'):
    return hashlib.new(algorithm, data).hexdigest()


def checksum(record, algorithm):
    checks = record.get('checksums', [])
    require(isinstance(checks, list) and all(isinstance(v, dict) for v in checks),
            'Invalid source checksum list')
    values = [v.get('checksumValue') for v in checks
              if v.get('algorithm') == algorithm.upper()]
    size = hashlib.new(algorithm).digest_size * 2
    require(len(values) == 1 and isinstance(values[0], str) and
            re.fullmatch('[0-9a-f]{' + str(size) + '}', values[0]),
            f'Missing/ambiguous {algorithm} source checksum')
    return values[0]


def definitions(receipts):
    require(set(receipts) == set(PORTS), 'Incomplete dependency receipt set')
    expected, resources, ports = {}, {}, {}
    for port in PORTS:
        spdx, copyright = receipts[port]
        require(copyright and len(copyright) <= MAX_JSON, 'Missing/oversized dependency notice')
        doc = parse_json(spdx)
        require(isinstance(doc, dict) and isinstance(doc.get('files'), list) and
                isinstance(doc.get('packages'), list), 'Invalid source receipt schema')
        require(all(isinstance(r, dict) for r in doc['files'] + doc['packages']),
                'Invalid source receipt record')
        ports[port] = port_info(spdx, port)
        records = doc.get('files', []) + doc.get('packages', [])
        ids = [v.get('SPDXID') for v in records]
        require(all(isinstance(v, str) for v in ids) and len(set(ids)) == len(ids),
                'Duplicate/invalid SPDX identity')
        recipe_names = set()
        for record in doc.get('files', []):
            if not record['SPDXID'].startswith('SPDXRef-port-file-'):
                continue
            name = record.get('fileName', '')
            require(isinstance(name, str) and name.startswith('./'), 'Expected relative recipe path')
            name = safe_name(name[2:])
            require(name.lower() not in recipe_names, 'Colliding recipe path')
            recipe_names.add(name.lower())
            expected[f'ports/{port}/{name}'] = ('sha256', checksum(record, 'sha256'))
        require({'portfile.cmake', 'vcpkg.json'} <= recipe_names, 'Incomplete port recipe')
        require(not any(str(parent).lower() in recipe_names for name in recipe_names
                        for parent in PurePosixPath(name).parents if str(parent) != '.'),
                'Recipe file/directory collision')
        source_records = [r for r in doc.get('packages', [])
                          if r['SPDXID'].startswith('SPDXRef-resource-')]
        require(0 < len(source_records) <= 8, 'Missing/excess source resources')
        for index, record in enumerate(source_records):
            resources[f'archives/{port}/{index}'] = checksum(record, 'sha512')
        for leaf, data in (('vcpkg.spdx.json', spdx), ('copyright', copyright)):
            expected[f'receipts/{port}/{leaf}'] = ('sha256', digest(data))
    require(len(expected) + len(resources) + 1 <= MAX_MEMBERS, 'Source member limit exceeded')
    return expected, resources, ports


def archive_suffix(data):
    if data.startswith(b'\x1f\x8b'):
        return '.tar.gz'
    if data.startswith(b'PK\x03\x04'):
        return '.zip'
    raise ValueError('Unsupported source archive format')


def manifest(payloads, ports, source_commit):
    require(re.fullmatch(r'[0-9a-f]{40}', source_commit), 'Invalid source commit')
    return {'schema': 1, 'sourceCommit': source_commit, 'scope': SCOPE, 'ports': ports,
            'files': {n: {'sha256': digest(b), 'size': len(b)} for n, b in sorted(payloads.items())}}


def verify_kit(blob, receipts, source_commit):
    require(len(blob) <= MAX_KIT, 'Source ZIP size limit exceeded')
    expected, resources, ports = definitions(receipts)
    payloads, names = {}, set()
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        infos = archive.infolist()
        require(len(infos) <= MAX_MEMBERS and sum(i.file_size for i in infos) <= MAX_KIT,
                'Source ZIP resource limit exceeded')
        for info in infos:
            require(info.orig_filename == info.filename, 'Normalized source path forbidden')
            name = safe_name(info.filename)
            require(name.lower() not in names and not info.is_dir(), 'Duplicate/directory source entry')
            names.add(name.lower())
            require(stat.S_IFMT(info.external_attr >> 16) in (0, stat.S_IFREG) and not info.flag_bits & 1,
                    'Linked/encrypted source entry forbidden')
            payloads[name] = archive.read(info)
    recorded = parse_json(payloads.pop('manifest.json', b'{}'))
    for prefix, sha512 in resources.items():
        choices = [n for n in (prefix + '.tar.gz', prefix + '.zip') if n in payloads]
        require(len(choices) == 1, 'Missing/ambiguous source archive')
        name = choices[0]
        require(name == prefix + archive_suffix(payloads[name]), 'Source format mismatch')
        expected[name] = ('sha512', sha512)
    require(set(payloads) == set(expected), 'Source membership mismatch')
    for name, (algorithm, wanted) in expected.items():
        require(digest(payloads[name], algorithm) == wanted, f'Source checksum mismatch: {name}')
    require(recorded == manifest(payloads, ports, source_commit), 'Source manifest mismatch')
    return {'ports': len(ports), 'archives': len(resources),
            'recipeFiles': sum(n.startswith('ports/') for n in payloads)}


def read_bounded(path, maximum=MAX_JSON):
    path = plain(path)
    require(0 < path.stat().st_size <= maximum, 'Source input size limit exceeded')
    with path.open('rb') as stream:
        data = stream.read(maximum + 1)
    require(0 < len(data) <= maximum, 'Source input size limit exceeded')
    return data


def cached_archives(downloads, wanted):
    downloads = plain(downloads, directory=True)
    candidates = list(downloads.iterdir())
    require(len(candidates) <= MAX_CACHE_FILES, 'Download cache file limit exceeded')
    found, total = {}, 0
    for path in sorted(candidates):
        if not path.name.endswith(('.tar.gz', '.zip')) or not path.is_file():
            continue
        path = plain(path)
        size = path.stat().st_size
        if size > MAX_KIT:
            continue
        total += size
        require(total <= MAX_CACHE_BYTES, 'Download cache scan budget exceeded')
        data = read_bounded(path, MAX_KIT)
        sha = digest(data, 'sha512')
        if sha in wanted:
            found[sha] = data
            if set(found) == wanted:
                return found
    require(set(found) == wanted, 'Missing exact source archive in download cache')
    return found


def export_kit(doc, stage, downloads=None):
    stage = destination(doc['buildRoot'], stage)
    root = plain(doc['vcpkgRoot'], directory=True)
    inputs = parse_json(read_bounded(stage / 'licenses/build-inputs.json'))
    receipts = {p: (read_bounded(stage / f'licenses/vcpkg/{p}/vcpkg.spdx.json'),
                    read_bounded(stage / f'licenses/vcpkg/{p}/copyright')) for p in PORTS}
    expected, resources, ports = definitions(receipts)
    require(ports == inputs['vcpkg'], 'Source receipts differ from build inputs')
    payloads = {}
    for name, (algorithm, wanted) in expected.items():
        if name.startswith('ports/'):
            data = read_bounded(root / name)
        else:
            _, port, leaf = name.split('/')
            data = receipts[port][0 if leaf == 'vcpkg.spdx.json' else 1]
        require(digest(data, algorithm) == wanted, f'Recipe/receipt checksum mismatch: {name}')
        payloads[name] = data
    found = cached_archives(downloads or root / 'downloads', set(resources.values()))
    for prefix, sha in resources.items():
        data = found[sha]
        payloads[prefix + archive_suffix(data)] = data
    payloads['manifest.json'] = json_bytes(manifest(payloads, ports, inputs['sourceCommit']))
    require(sum(map(len, payloads.values())) <= MAX_KIT, 'Source material size limit exceeded')
    target = stage / KIT
    # All inputs validated first. A failed write/verification leaves the old kit intact.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, prefix='.source-', suffix='.tmp', delete=False) as tmp:
            temporary = Path(tmp.name)
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_STORED) as archive:
            for name, data in sorted(payloads.items()):
                entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                entry.create_system = 3
                entry.external_attr = (stat.S_IFREG | 0o644) << 16
                archive.writestr(entry, data)
        summary = verify_kit(read_bounded(temporary, MAX_KIT), receipts, inputs['sourceCommit'])
        os.replace(temporary, target)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(f'PASS: source materials {summary}; rebuild/other-component gates remain open')
    return summary


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--downloads', type=Path)
    args = parser.parse_args()
    try:
        export_kit(config(args.config), args.stage, args.downloads)
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as exc:
        parser.exit(1, f'Source material gate failed: {exc}\n')
