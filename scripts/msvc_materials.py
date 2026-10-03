"""DEC-0178: exact runtime versions and signed installer terms, not entitlement."""
import ctypes
from ctypes import wintypes
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import xml.etree.ElementTree as ET

from package_inventory import MAX_JSON, json_bytes, parse_json, require, sha256
from stage_runtime import plain

EVIDENCE = 'licenses/msvc/runtime-materials.json'
LICENSE = 'licenses/msvc/license.rtf'
TERMS = 'licenses/msvc/README.txt'
MAX_INSTALLER = 64 * 1024 * 1024
README = b'''Microsoft Visual C++ runtime materials

The unmodified Microsoft-signed vc_redist.x64.exe and app-local runtime DLLs
come from the configured Visual Studio installation. runtime-materials.json
records actual binary versions, SHA256 hashes, Authenticode inspection and
the installed bundle's embedded license.rtf. The directory label is NOT the
runtime version. No installer was executed to collect these materials.

The embedded RTF is the END-USER license. It does not grant SDR Town's publisher
redistribution rights by itself. Publisher entitlement and redistribution are
governed separately by the applicable Visual Studio license and permitted-file
list: https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution
https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files
Do not treat this receipt, MIT project license, or a successful package build
as legal clearance. These runtime files are not MIT and must not be modified.
'''


def file_version(path):
    """Windows VERSIONINFO fixed fields, not the misleading parent directory label."""
    api = ctypes.WinDLL('version', use_last_error=True)
    api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    api.GetFileVersionInfoW.restype = wintypes.BOOL
    api.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR,
                                 ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    api.VerQueryValueW.restype = wintypes.BOOL
    unused = wintypes.DWORD()
    size = api.GetFileVersionInfoSizeW(str(path), ctypes.byref(unused))
    require(0 < size <= MAX_JSON, 'Missing/oversized runtime VERSIONINFO')
    buffer = ctypes.create_string_buffer(size)
    require(api.GetFileVersionInfoW(str(path), 0, size, buffer), 'Cannot read runtime VERSIONINFO')
    pointer, length = ctypes.c_void_p(), wintypes.UINT()
    require(api.VerQueryValueW(buffer, '\\', ctypes.byref(pointer), ctypes.byref(length)) and
            length.value >= 13 * 4, 'Missing fixed runtime VERSIONINFO')
    fields = ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD))
    require(fields[0] == 0xFEEF04BD, 'Invalid fixed runtime VERSIONINFO')
    return '.'.join(str(n) for n in (fields[2] >> 16, fields[2] & 65535,
                                    fields[3] >> 16, fields[3] & 65535))


def payload(xml):
    require(len(xml) <= MAX_JSON and b'<!DOCTYPE' not in xml.upper() and b'<!ENTITY' not in xml.upper(),
            'Unsafe or oversized redistributable manifest')
    try:
        root = ET.fromstring(xml)
    except ET.ParseError as exc:
        raise ValueError('Malformed redistributable manifest') from exc
    ns = {'b': 'http://schemas.microsoft.com/wix/2008/Burn'}
    registration = root.findall('b:Registration', ns)
    candidates = [p for p in root.findall('b:UX/b:Payload', ns) if p.get('FilePath') == 'license.rtf']
    require(len(registration) == 1 and len(candidates) == 1, 'Ambiguous redistributable license/identity')
    version, item = registration[0].get('Version', ''), candidates[0]
    require(re.fullmatch(r'\d+\.\d+\.\d+\.\d+', version), 'Invalid redistributable bundle version')
    source = item.get('SourcePath', '')
    require(re.fullmatch(r'u\d+', source) and item.get('Packaging') == 'embedded', 'Unsafe license payload')
    size = int(item.get('FileSize', '0'))
    checksum = item.get('Hash', '').lower()
    require(0 < size <= MAX_JSON and re.fullmatch(r'[0-9a-f]{40}', checksum), 'Invalid license receipt')
    return version, source, size, checksum


def extract(tool, installer, member, maximum):
    # The installer is signature-checked first. Only a single bounded named
    # payload is requested; never run the installer or extract its paths to disk.
    with subprocess.Popen([tool, 'x', '-so', str(installer), member],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as process:
        deadline = threading.Timer(60, process.kill)
        deadline.start()
        try:
            data = process.stdout.read(maximum + 1)
            if len(data) > maximum:
                process.kill()
                raise ValueError('Redistributable payload size limit exceeded')
            require(process.wait() == 0, 'Redistributable extraction failed or timed out')
            return data
        finally:
            deadline.cancel()


def collect(redist, runtime):
    installer = plain(redist / 'vc_redist.x64.exe')
    require(installer.stat().st_size <= MAX_INSTALLER, 'Redistributable size limit exceeded')
    command = shutil.which('powershell.exe')
    tool = shutil.which('7z')
    if not tool:
        installed = Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / '7-Zip/7z.exe'
        tool = str(plain(installed)) if installed.is_file() else None
    require(command and tool, 'PowerShell and 7-Zip required for exact Microsoft terms collection')
    signature = subprocess.run([command, '-NoProfile', '-NonInteractive', '-File',
                                str(Path(__file__).with_name('runtime_signature.ps1')),
                                '-Path', str(installer)], capture_output=True, check=True, timeout=90)
    signed = parse_json(signature.stdout)
    require(signed.get('status') == 'Valid' and 'Microsoft Corporation' in signed.get('subject', ''),
            'Microsoft redistributable signature check failed')
    xml = extract(tool, installer, '0', MAX_JSON)
    version, member, size, checksum = payload(xml)
    license_data = extract(tool, installer, member, size)
    require(len(license_data) == size and hashlib.sha1(license_data).hexdigest() == checksum,
            'Embedded Microsoft license checksum mismatch')
    versions = {name: file_version(path) for name, path in sorted(runtime.items())}
    require(all(v == version for v in versions.values()), 'MSVC DLL/bundle version mismatch')
    require(file_version(installer) == version, 'MSVC installer/bundle version mismatch')
    hashes = {name: sha256(path) for name, path in sorted(runtime.items())}
    hashes[installer.name] = sha256(installer)
    doc = {'schema': 1, 'directoryLabel': redist.name, 'runtimeVersion': version,
           'fileVersions': versions, 'runtimeSha256': hashes, 'installerSignature': signed,
           'bundleManifestSha256': hashlib.sha256(xml).hexdigest(),
           'licenseSha256': hashlib.sha256(license_data).hexdigest(),
           'licensePayload': {'member': member, 'size': size, 'sha1': checksum},
           'scope': 'Signed runtime/end-user terms evidence; publisher entitlement not certified'}
    return installer, {EVIDENCE: json_bytes(doc), LICENSE: license_data, TERMS: README}


def verify(read, entries, deployment):
    doc = parse_json(read(EVIDENCE))
    require(doc.get('schema') == 1 and doc.get('directoryLabel') == deployment.get('msvcRedistVersion') and
            doc.get('runtimeVersion') == deployment.get('msvcRuntimeVersion') and
            re.fullmatch(r'\d+\.\d+\.\d+\.\d+', doc.get('runtimeVersion', '')),
            'MSVC material identity mismatch')
    hashes = {n: e['sha256'] for n, e in entries.items() if e['component'] == 'msvc-runtime'}
    require('vc_redist.x64.exe' in hashes and doc.get('runtimeSha256') == hashes,
            'MSVC material runtime checksum mismatch')
    require(doc.get('fileVersions') == {n: doc['runtimeVersion'] for n in hashes if n.endswith('.dll')},
            'MSVC material DLL version mismatch')
    license_data = read(LICENSE)
    info = doc.get('licensePayload', {})
    require(info.get('size') == len(license_data) and
            info.get('sha1') == hashlib.sha1(license_data).hexdigest() and
            doc.get('licenseSha256') == hashlib.sha256(license_data).hexdigest(),
            'MSVC license checksum mismatch')
    require(read(TERMS) == README, 'MSVC redistribution distinction missing')
    signature = doc.get('installerSignature', {})
    require(signature.get('status') == 'Valid' and
            'Microsoft Corporation' in signature.get('subject', '') and
            re.fullmatch(r'[0-9A-Fa-f]{40}', signature.get('thumbprint', '')),
            'MSVC installer signature evidence missing')
    return {'runtimeVersion': doc['runtimeVersion'], 'directoryLabel': doc['directoryLabel'],
            'dllCount': len(doc['fileVersions']), 'installerSha256': hashes['vc_redist.x64.exe'],
            'licenseSha256': doc['licenseSha256'], 'publisherEntitlementVerified': False}
