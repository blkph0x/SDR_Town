"""DEC-0179: rebuild pinned Qt runtimes in isolation and test actual app replacement."""
import argparse
import ctypes
import hashlib
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile

from package_inventory import MAX_BYTES, MAX_FILES, component, json_bytes, require, sha256, tree_files
from qt_sources import CONFIG_FILES, KIT, archive_name, verify
from stage_runtime import config, plain


# ISS-0124: reuse the package resource budget and AppBootstrap's 8 KiB launch
# tail budget. Evidence excludes settings, source kits and raw launch extras.
LAUNCH_LOG_LIMIT = 8192
LAUNCH_STAGES = (
    'main-entry', 'cli-path', 'before-qapp', 'before-mainwindow', 'entering-exec',
    'gui-startup-settled', 'gui-audio-prewarm-ok', 'gui-audio-prewarm-no-output',
    'gui-audio-prewarm-failed', 'seh-unhandled', 'std-exception', 'unknown-exception',
    'shutdown-std-exception', 'shutdown-unknown-exception',
)


def optional_evidence(operation):
    """An evidence failure must never replace the original QA exception."""
    try:
        return operation()
    except BaseException as exc:
        # Exception messages can contain paths, command arguments or secrets.
        return {'status': 'unavailable', 'errorType': type(exc).__name__}


def launch_log_state(path):
    if not path.exists() and not path.is_symlink():
        return {'exists': False}
    stat = plain(path).stat()
    return {'exists': True, 'identity': (stat.st_dev, stat.st_ino), 'size': stat.st_size}


def preserve_launch_log(path, baseline, started_ns, output):
    if 'exists' not in baseline:
        return {'status': 'omitted', 'reason': 'no-safe-baseline'}
    if not path.exists():
        return {'status': 'omitted', 'reason': 'missing'}
    path = plain(path)
    with path.open('rb') as source:
        stat = os.fstat(source.fileno())
        if stat.st_mtime_ns < started_ns:
            return {'status': 'omitted', 'reason': 'predates-run'}
        offset = baseline.get('size', 0)
        if baseline['exists'] and (
                baseline['identity'] != (stat.st_dev, stat.st_ino) or stat.st_size < offset):
            return {'status': 'omitted', 'reason': 'replaced-or-truncated'}
        begin = max(offset, stat.st_size - LAUNCH_LOG_LIMIT)
        source.seek(begin)
        data = source.read(stat.st_size - begin)
        after = os.fstat(source.fileno())
        if (after.st_size, after.st_mtime_ns) != (stat.st_size, stat.st_mtime_ns):
            return {'status': 'omitted', 'reason': 'changed-during-read'}
    if begin > offset:
        data = data.partition(b'\n')[2]  # Never treat a partial tail as a marker.
    markers = []
    pattern = rb'\[(\d{1,20})\] stage=(' + b'|'.join(s.encode('ascii') for s in LAUNCH_STAGES) + rb')(?: extra=(.*))?'
    for line in data.splitlines(keepends=True):
        if not line.endswith(b'\n'):
            continue
        match = re.fullmatch(pattern, line.rstrip(b'\r\n'))
        if not match:
            continue
        stamp, stage, extra = match.groups()
        marker = b'[' + stamp + b'] stage=' + stage
        if extra is not None:
            # Only numeric SEH codes are safe; free-form text is discarded.
            code = re.fullmatch(rb'code=(\d{1,10})', extra) if stage == b'seh-unhandled' else None
            marker += b' extra=' + (extra if code and int(code[1]) <= 0xffffffff else b'[redacted]')
        markers.append(marker + b'\n')
    while sum(map(len, markers)) > LAUNCH_LOG_LIMIT:
        markers.pop(0)
    if not markers:
        return {'status': 'omitted', 'reason': 'no-new-markers'}
    target = plain(output, directory=True) / 'launch.log'
    with target.open('xb') as destination:
        destination.write(b''.join(markers))
    return {'status': 'saved', 'file': target.name, 'bytes': target.stat().st_size,
            'scope': 'new known launch markers only; free-form extras redacted'}


def symbol_identity(path):
    # Microsoft SYMSRV_INDEX_INFOW: local PE/PDB GUID and age, no symbol server.
    # https://learn.microsoft.com/windows/win32/api/dbghelp/ns-dbghelp-symsrv_index_infow
    # This harness calls DbgHelp serially, as required by that API.
    require(os.name == 'nt', 'PDB inspection requires Windows')

    class Guid(ctypes.Structure):
        _fields_ = [('data1', ctypes.c_uint32), ('data2', ctypes.c_uint16),
                    ('data3', ctypes.c_uint16), ('data4', ctypes.c_ubyte * 8)]

    class IndexInfo(ctypes.Structure):
        _fields_ = [('sizeofstruct', ctypes.c_uint32), ('file', ctypes.c_wchar * 261),
                    ('stripped', ctypes.c_int32), ('timestamp', ctypes.c_uint32),
                    ('size', ctypes.c_uint32), ('dbgfile', ctypes.c_wchar * 261),
                    ('pdbfile', ctypes.c_wchar * 261), ('guid', Guid),
                    ('sig', ctypes.c_uint32), ('age', ctypes.c_uint32)]

    library = plain(Path(os.environ['SYSTEMROOT']) / 'System32/DbgHelp.dll')
    api = ctypes.WinDLL(str(library), use_last_error=True).SymSrvGetFileIndexInfoW
    api.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(IndexInfo), ctypes.c_uint32]
    api.restype = ctypes.c_int32
    info = IndexInfo()
    info.sizeofstruct = ctypes.sizeof(info)
    require(api(str(plain(path)), ctypes.byref(info), 0), 'Symbol identity unavailable')
    require(any(bytes(info.guid)) and info.sig == 0, 'Modern PDB identity unavailable')
    return bytes(info.guid), info.age


def prepare_app_pdb(doc, portable):
    # Only the declared build's adjacent PDB is considered, never a directory search.
    executable = plain(Path(doc['builtFiles']['SDR_Town.exe']))
    candidate = executable.with_suffix('.pdb')
    if not candidate.exists() and not candidate.is_symlink():
        return {'status': 'omitted', 'reason': 'not-built'}
    candidate = plain(candidate)
    portable = plain(portable, directory=True)
    application = plain(portable / 'SDR_Town.exe')
    require(sha256(executable) == sha256(application), 'Build executable does not match prepared runtime')
    files = tree_files(portable)
    size = candidate.stat().st_size
    require(len(files) < MAX_FILES and sum(p.stat().st_size for p in files.values()) + size <= MAX_BYTES,
            'Symbols exceed evidence resource limit')
    identity = symbol_identity(application)
    require(identity == symbol_identity(candidate), 'PDB GUID/age mismatch')
    target = portable / 'SDR_Town.pdb'
    with candidate.open('rb') as source, target.open('xb') as destination:
        count = 0
        for block in iter(lambda: source.read(1024 * 1024), b''):
            count += len(block)
            require(count <= size, 'PDB grew during preparation')
            destination.write(block)
    require(count == size, 'PDB size changed during preparation')
    require(symbol_identity(target) == identity, 'Prepared PDB GUID/age mismatch')
    return {'status': 'matched', 'sha256': sha256(target)}


def prepared_runtime(portable, include_pdb=False):
    files = tree_files(plain(portable, directory=True))
    return {name: {'sha256': sha256(path), 'bytes': path.stat().st_size}
            for name, path in files.items()
            if path.suffix.lower() in ('.exe', '.dll') or name == 'qt.conf'
            or (include_pdb and name == 'SDR_Town.pdb')}


def archive_failure_runtime(root, portable, prepared, output):
    root = plain(root, directory=True)
    portable = plain(portable, directory=True)
    output = plain(output, directory=True)
    require(root.name.startswith('qt-replacement-') and portable == root / 'portable',
            'Evidence must use the prepared temporary portable tree')
    require(not output.is_relative_to(root), 'Evidence output must be outside the disposable runtime')
    files = tree_files(portable)  # Reject links/reparse entries and unsafe or oversized trees.
    require('SDR_Town.exe' in prepared and len(prepared) <= MAX_FILES, 'Missing prepared runtime')
    require(sum(entry['bytes'] for entry in prepared.values()) <= MAX_BYTES, 'Evidence resource limit exceeded')
    for name, entry in prepared.items():
        require(name in files and (Path(name).suffix.lower() in ('.exe', '.dll')
                or name in ('qt.conf', 'SDR_Town.pdb')), 'Unprepared evidence file')
        require(files[name].stat().st_size == entry['bytes'], 'Prepared runtime size changed')
    temporary = output / 'portable.zip.partial'
    with zipfile.ZipFile(temporary, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, entry in sorted(prepared.items()):
            path = plain(files[name])
            require(path.is_relative_to(portable), 'Evidence file escaped portable tree')
            digest, count = hashlib.sha256(), 0
            with path.open('rb') as source, archive.open(name, 'w') as destination:
                for block in iter(lambda: source.read(1024 * 1024), b''):
                    count += len(block)
                    require(count <= entry['bytes'], 'Prepared runtime grew during archival')
                    digest.update(block)
                    destination.write(block)
            require(count == entry['bytes'] and digest.hexdigest() == entry['sha256'],
                    'Prepared runtime contents changed')
    target = output / 'portable.zip'
    require(not target.exists() and not target.is_symlink(), 'Evidence archive already exists')
    temporary.rename(target)
    return {'status': 'saved', 'file': target.name, 'sha256': sha256(target), 'files': prepared}


def preserve_failure_evidence(root, portable, prepared, output, launch_path, baseline, started_ns):
    output = plain(output, directory=True)
    directory = Path(tempfile.mkdtemp(prefix='failure-', dir=output))
    require(plain(directory, directory=True).parent == output, 'Unsafe evidence directory')
    metadata = {
        'runtime': optional_evidence(lambda: archive_failure_runtime(root, portable, prepared, directory))
            if prepared else {'status': 'omitted', 'reason': 'runtime-not-prepared'},
        'launchLog': optional_evidence(lambda: preserve_launch_log(launch_path, baseline, started_ns, directory)),
    }
    (directory / 'evidence.json').write_bytes(json_bytes(metadata))
    return {'directory': directory.name, **metadata}


def run(command, log, env=None, timeout=900):
    command = [str(x) for x in command]
    if log.exists() or log.is_symlink():
        plain(log)
    with log.open('wb') as output:
        output.write(json_bytes({'command': command, 'timeoutSeconds': timeout}))
        output.flush()
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, env=env)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            # Kill only this QA process tree; cmake can otherwise leave ninja/cl alive.
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                           stdout=output, stderr=subprocess.STDOUT, timeout=30)
            process.wait(timeout=30)
            raise
        if code:
            raise subprocess.CalledProcessError(code, command)


def extract_module(data, source):
    """Called only after kit verification; still enforce containment on extraction."""
    source.mkdir()
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:xz') as archive:
        for entry in archive:
            require(entry.isfile() or entry.isdir(), 'Non-regular Qt source entry')
            if not entry.isfile():
                continue
            relative = Path(*entry.name.split('/')[1:])
            target = source / relative
            require(target.resolve().is_relative_to(source), 'Unsafe Qt source extraction')
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(entry) as inp, target.open('wb') as out:
                shutil.copyfileobj(inp, out)


def replacement_files(files, install, full):
    names = [n for n in files if n != 'package-inventory.json' and component(n) == 'qt'] if full else [
        'Qt6Svg.dll', 'imageformats/qsvg.dll', 'iconengines/qsvgicon.dll']
    require(bool(names), 'No Qt runtimes selected for replacement')
    result = {}
    for name in names:
        require(name in files and name.lower().endswith('.dll'), 'Unexpected Qt runtime entry')
        source = install / ('plugins' if '/' in name else 'bin') / name
        result[name] = plain(source)
    return result


def feature_differences(sdk, rebuilt):
    differences = {}
    for name in CONFIG_FILES:
        original, replacement = plain(sdk / name).read_bytes(), plain(rebuilt / name).read_bytes()
        # Compare feature values, not header guards or whitespace across build generators.
        pattern = rb'^#define (QT_FEATURE_\w+) (-?\d+)\s*$'
        a = dict(re.findall(pattern, original, re.M))
        b = dict(re.findall(pattern, replacement, re.M))
        changed = {k.decode(): {'sdk': a.get(k, b'absent').decode(),
                               'rebuilt': b.get(k, b'absent').decode()}
                   for k in sorted(a.keys() | b.keys()) if a.get(k) != b.get(k)}
        if changed:
            differences[name] = changed
    return differences


def base_options(openssl_root=None):
    # These bundled alternatives exist in both pinned qtbase releases. Auto
    # discovery otherwise links RadioConda's DLLs on the developer host.
    bundled = ('zlib', 'pcre2', 'doubleconversion', 'libb2', 'freetype',
               'harfbuzz', 'jpeg', 'png', 'textmarkdownreader', 'sqlite')
    return [f'-DFEATURE_system_{name}=OFF' for name in bundled] + [
        '-DBUILD_SHARED_LIBS=ON', '-DFEATURE_brotli=OFF', '-DFEATURE_zstd=OFF',
        '-DFEATURE_icu=OFF', '-DFEATURE_schannel=ON',
        '-DFEATURE_openssl=' + ('ON' if openssl_root else 'OFF'),
        '-DFEATURE_openssl_linked=OFF'] + (
            [f'-DOPENSSL_ROOT_DIR={openssl_root}', f'-DOPENSSL_INCLUDE_DIR={openssl_root}/include']
            if openssl_root else [])


def check_base_cache(path, openssl_root=None):
    values = dict(re.findall(r'^([A-Za-z0-9_]+):[^=\r\n]+=([^\r\n]*)$', path.read_text(), re.M))
    for option in base_options(openssl_root):
        name, value = option[2:].split('=', 1)
        if name.startswith('OPENSSL_'):
            require(Path(values.get(name, '')).resolve() == Path(value).resolve(),
                    f'Qt OpenSSL header input mismatch: {name}')
            continue
        require(values.get(name, '').strip() == value, f'Qt source configuration mismatch: {name}')
        computed = 'QT_' + name if name.startswith('FEATURE_') else 'QT_FEATURE_shared'
        require(values.get(computed, '').strip() == value, f'Qt computed configuration mismatch: {computed}')


def runtime_environment(portable):
    env = {k.upper(): v for k, v in os.environ.items()}
    windows = plain(Path(env['SYSTEMROOT']), directory=True)
    env['PATH'] = os.pathsep.join(map(str, (portable, windows / 'System32', windows)))
    env['QT_QPA_PLATFORM'] = 'windows'
    for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QTDIR'):
        env.pop(key, None)
    return env


def desktop_metrics():
    # Raw logical desktop dimensions, not an assumption about Qt's DPI scale.
    class Rect(ctypes.Structure):
        _fields_ = [(n, ctypes.c_long) for n in ('left', 'top', 'right', 'bottom')]
    api = ctypes.WinDLL('user32', use_last_error=True)
    api.GetSystemMetrics.argtypes = [ctypes.c_int]
    api.GetSystemMetrics.restype = ctypes.c_int
    area = Rect()
    api.SystemParametersInfoW.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint]
    api.SystemParametersInfoW.restype = ctypes.c_int
    ok = api.SystemParametersInfoW(0x0030, 0, ctypes.byref(area), 0)  # SPI_GETWORKAREA, WinUser.h
    return {'primaryWidth': api.GetSystemMetrics(0), 'primaryHeight': api.GetSystemMetrics(1),
            'workArea': {n: getattr(area, n) for n, _ in area._fields_} if ok else None}


def test(doc, stage, output, repo, full=False, work_root=None, openssl_root=None, headless_layout=False):
    stage, repo = plain(stage, directory=True), plain(repo, directory=True)
    require(not output.resolve().is_relative_to(stage), 'Replacement output must not be inside package')
    output.mkdir(parents=True, exist_ok=True)
    output = plain(output, directory=True)
    require(not output.is_relative_to(stage), 'Replacement output must not be inside package')
    result = {'schema': 2, 'status': 'running', 'qtVersion': doc['qtVersion'],
              'requestedScope': 'packaged-base-and-svg' if full else 'svg',
              'fullQtRebuildVerified': False}
    result_path = output / 'result.json'
    if result_path.exists() or result_path.is_symlink():
        plain(result_path)
    result_path.write_bytes(json_bytes(result))
    started = time.monotonic()
    try:
        require(not headless_layout or full, 'Headless layout requires the full Qt source build')
        perform_test(doc, stage, output, repo, full, work_root, result, openssl_root, headless_layout)
    except BaseException as exc:
        result.update(status='failed', error=f'{type(exc).__name__}: {exc}', fullQtRebuildVerified=False)
        raise
    finally:
        result['elapsedSeconds'] = round(time.monotonic() - started, 3)
        if result['status'] == 'failed':
            optional_evidence(lambda: result_path.write_bytes(json_bytes(result)))
        else:
            result_path.write_bytes(json_bytes(result))


def perform_test(doc, stage, output, repo, full, work_root, result, openssl_root, headless_layout):
    files = tree_files(stage)
    evidence = verify(files[KIT].read_bytes(), doc['qtVersion'])
    before = {n: sha256(p) for n, p in files.items()}
    result.update(sourceKitSha256=evidence['kitSha256'], applicationSha256=before['SDR_Town.exe'])
    result['originalQtSha256'] = {n: h for n, h in before.items()
                                 if n != 'package-inventory.json' and component(n) == 'qt'}
    result['desktopMetrics'] = desktop_metrics()
    if full and 'tls/qopensslbackend.dll' in files:
        require(openssl_root is not None, 'Package includes OpenSSL backend: explicit --openssl-root required')
        openssl_root = plain(openssl_root, directory=True)
        headers = tree_files(plain(openssl_root / 'include/openssl', directory=True))
        require({'ssl.h', 'crypto.h', 'opensslv.h'} <= headers.keys(), 'Incomplete OpenSSL header input')
        result['opensslHeaders'] = {'files': {n: sha256(p) for n, p in headers.items()},
                                   'scope': 'Build headers only; runtime-loaded backend, no OpenSSL DLL deployment'}
    else:
        require(openssl_root is None, 'OpenSSL headers supplied but package has no OpenSSL backend')
    qt = plain(Path(doc['qtBin']).parent, directory=True)
    vc = next(p for p in plain(doc['compiler']).parents if p.name == 'VC')
    setup = plain(vc / 'Auxiliary/Build/vcvars64.bat')
    require(not any(c in str(setup) for c in '&|<>%^"'), 'Unsafe compiler environment path')
    # Use the SDK-supported Ninja generator with this configured VS environment.
    setup_result = subprocess.run(f'cmd.exe /d /u /s /c ""{setup}" >nul && set"',
                                  check=True, capture_output=True, timeout=60)
    build_env = {k.upper(): v for k, v in os.environ.items()}
    for line in setup_result.stdout.decode('utf-16-le').splitlines():
        key, separator, value = line.partition('=')
        if separator and key:
            build_env[key.upper()] = value
    candidates = [shutil.which('ninja', path=build_env.get('PATH', build_env.get('Path'))),
                  qt.parent.parent / 'Tools/Ninja/ninja.exe',
                  vc.parent / 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe']
    ninja = next((plain(p) for p in candidates if p and Path(p).is_file()), None)
    require(ninja is not None, 'Ninja required for the Qt source replacement test')
    # Build sources must not find the pre-installed SDK or ambient vcpkg toolchain.
    for key in ('CMAKE_PREFIX_PATH', 'CMAKE_TOOLCHAIN_FILE', 'Qt6_DIR', 'QT6_DIR',
                'QT_HOST_PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QTDIR'):
        build_env.pop(key, None)
    work_root = work_root or output
    require(not work_root.resolve().is_relative_to(stage), 'Build workspace must not be inside package')
    work_root.mkdir(parents=True, exist_ok=True)
    work_root = plain(work_root, directory=True)
    require(not work_root.is_relative_to(stage), 'Build workspace must not be inside package')
    temporary = tempfile.TemporaryDirectory(prefix='qt-replacement-', dir=work_root)
    root = Path(temporary.name).resolve()
    # Validate the exact absolute cleanup target before TemporaryDirectory owns it.
    require(root.parent == work_root and root.name.startswith('qt-replacement-'), 'Unsafe temporary build path')
    install, portable = root / 'install', root / 'portable'
    prepared = None
    failed = False
    # A private child TEMP prevents collecting an operator's or another run's
    # launch log. Both Windows GetTempPath and Qt's temporary settings use it.
    runtime_temp = root / 'runtime-temp'
    launch_path = runtime_temp / 'sdr_town_launch.log'
    baseline = {'exists': False}
    started_ns = time.time_ns()
    try:
        cmake_options = ['-G', 'Ninja', f'-DCMAKE_MAKE_PROGRAM={ninja}',
                         f'-DCMAKE_C_COMPILER={doc["compiler"]}',
                         f'-DCMAKE_CXX_COMPILER={doc["compiler"]}',
                         f'-DCMAKE_INSTALL_PREFIX={install}',
                         '-DQT_BUILD_INTERNALS_NO_FORCE_SET_INSTALL_PREFIX=ON',
                         '-DCMAKE_BUILD_TYPE=Release', '-DQT_BUILD_TESTS=OFF',
                         '-DQT_BUILD_EXAMPLES=OFF', '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF',
                         '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF']
        modules = ('qtbase', 'qtsvg') if full else ('qtsvg',)
        with zipfile.ZipFile(files[KIT]) as kit:
            for module in modules:
                source, build = root / module, root / (module + '-build')
                extract_module(kit.read('sources/' + archive_name(module, doc['qtVersion'])), source)
                prefix = install if full else qt
                options = cmake_options + [f'-DCMAKE_PREFIX_PATH={prefix}']
                if module == 'qtbase':
                    options += base_options(openssl_root)
                print(f'Building {module} {doc["qtVersion"]} from packaged source', flush=True)
                run(['cmake', '-S', source, '-B', build, *options],
                    output / f'{module}-configure.log', build_env)
                for leaf in ('config.summary', 'CMakeCache.txt'):
                    if (build / leaf).is_file():
                        shutil.copyfile(build / leaf, output / f'{module}-{leaf}')
                if module == 'qtbase':
                    check_base_cache(build / 'CMakeCache.txt', openssl_root)
                run(['cmake', '--build', build, '--config', 'Release', '-j', '4'],
                    output / f'{module}-build.log', build_env, timeout=3600)
                run(['cmake', '--install', build, '--config', 'Release'],
                    output / f'{module}-install.log', build_env)
                for leaf in ('config.summary', 'CMakeCache.txt'):
                    if (build / leaf).is_file():
                        shutil.copyfile(build / leaf, output / f'{module}-{leaf}')
        if full:
            result['sdkFeatureDifferences'] = feature_differences(qt, install)
        for name, path in files.items():
            if name == 'package-inventory.json':
                continue
            target = portable / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
        replacements = replacement_files(files, install, full)
        rebuilt = result['rebuiltSha256'] = {}
        for name, path in replacements.items():
            plain(path)
            shutil.copyfile(path, portable / name)
            rebuilt[name] = sha256(portable / name)
        result['applicationSymbols'] = optional_evidence(lambda: prepare_app_pdb(doc, portable))
        probe = root / 'probe'
        run(['cmake', '-S', repo / 'tests/qt_replacement_probe', '-B', probe,
             *cmake_options, f'-DCMAKE_PREFIX_PATH={install}' if full else f'-DCMAKE_PREFIX_PATH={install};{qt}',
             f'-DQt6Svg_DIR={install}/lib/cmake/Qt6Svg'], output / 'probe-configure.log', build_env)
        run(['cmake', '--build', probe, '--config', 'Release', '-j', '4'], output / 'probe-build.log', build_env)
        shutil.copyfile(probe / 'qt_replacement_probe.exe', portable / 'qt_replacement_probe.exe')
        prepared = prepared_runtime(portable, result['applicationSymbols']['status'] == 'matched')
        # The portable package intentionally ships qwindows, not qoffscreen.
        # No window is shown by the probe; rasterization runs on QImages.
        env = runtime_environment(portable)
        runtime_temp.mkdir()
        env.update(TEMP=str(runtime_temp), TMP=str(runtime_temp))
        baseline = optional_evidence(lambda: launch_log_state(launch_path))
        started_ns = time.time_ns()
        run([portable / 'qt_replacement_probe.exe'], output / 'probe.log', env, timeout=30)
        run([portable / 'SDR_Town.exe', '--allow-multiple', '--no-control-server',
             '--no-remote-diagnostics', '--cli', '--cmd', 'help'], output / 'cli.log', env, timeout=45)
        require('biastee <device>' in (output / 'cli.log').read_text(errors='replace'),
                'Replaced Qt CLI did not reach command handling')
        if full:
            if headless_layout:
                run([sys.executable, repo / 'scripts/test_workspace_gui.py', '--exe', portable / 'SDR_Town.exe',
                     '--output', output / 'gui-native', '--only-profile', 'listening'],
                    output / 'gui-native.log', env, timeout=60)
                result['nativeApplicationGui'] = 'listening-pass'
                # DEC-0180: exact viewport layout QA must not depend on runner screen size.
                # This auxiliary plugin is deliberately NOT part of the original package.
                plugin = plain(install / 'plugins/platforms/qoffscreen.dll')
                shutil.copyfile(plugin, portable / 'platforms/qoffscreen.dll')
                prepared = prepared_runtime(portable, result['applicationSymbols']['status'] == 'matched')
                result['auxiliaryQaPlugins'] = {'platforms/qoffscreen.dll': sha256(plugin)}
                # Qt's generic offscreen font database uses QT_QPA_FONTDIR,
                # otherwise a portable copy has no fonts and renders tofu.
                fonts = plain(Path(env['SYSTEMROOT']) / 'Fonts', directory=True)
                env = dict(env, QT_QPA_PLATFORM='offscreen', QT_QPA_FONTDIR=str(fonts))
                result['offscreenFonts'] = 'Windows installed fonts; not copied or redistributed'
            result['applicationGuiBackend'] = env['QT_QPA_PLATFORM']
            run([sys.executable, repo / 'scripts/test_workspace_gui.py', '--exe', portable / 'SDR_Town.exe',
                 '--output', output / 'gui'], output / 'gui.log', env, timeout=240)
        require(before == {n: sha256(p) for n, p in tree_files(stage).items()},
                'Replacement test modified original package')
        result.update(status='pass', rebuiltSha256=rebuilt, rendererAndPlugins='pass',
                      widgetsAndLoopbackNetwork='pass', nativeTlsBackend='pass', applicationCli='pass',
                      applicationGui='four-no-rx-profiles-pass' if full else 'not-run',
                      originalPackageUnchanged=True, fullQtRebuildVerified=full,
                      rebuildScope='packaged qtbase/qtsvg runtime; not qttools or all Qt modules')
        print(f'PASS: Qt {doc["qtVersion"]} {result["requestedScope"]}; original package unchanged')
    except BaseException:
        failed = True
        result['failureEvidence'] = optional_evidence(lambda: preserve_failure_evidence(
            root, portable, prepared, output, launch_path, baseline, started_ns))
        raise
    finally:
        if failed:
            result['temporaryCleanup'] = optional_evidence(temporary.cleanup)
        else:
            temporary.cleanup()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--full', action='store_true', help='Rebuild all packaged Qtbase/QtSvg runtimes')
    parser.add_argument('--work-root', type=Path, help='Short disposable build parent, no preexisting files removed')
    parser.add_argument('--openssl-root', type=Path, help='Explicit header SDK when package includes qopensslbackend')
    parser.add_argument('--headless-layout', action='store_true',
                        help='Native listening smoke plus all exact-size GUI profiles on QA-only qoffscreen')
    args = parser.parse_args()
    test(config(args.config), args.stage, args.output, Path(__file__).resolve().parent.parent,
         args.full, args.work_root, args.openssl_root, args.headless_layout)
