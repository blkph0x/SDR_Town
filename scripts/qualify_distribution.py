"""DEC-0196: bind successful replacement qualification to the shipped package."""
import argparse
from pathlib import Path
import re

from package_inventory import json_bytes, parse_json, require, sha256, component, tree_files

QUALIFICATION = 'licenses/distribution/qualification.json'
USB = ('rtlsdr.dll', 'libusb-1.0.dll', 'pthreadVC3.dll')
QT_KEYS = ('status', 'fullQtRebuildVerified', 'originalPackageUnchanged',
           'applicationSha256', 'qtVersion', 'sourceKitSha256', 'rebuiltSha256', 'originalQtSha256',
           'applicationCli', 'applicationGui', 'nativeTlsBackend',
           'rendererAndPlugins', 'widgetsAndLoopbackNetwork')
USB_KEYS = ('status', 'sourceCommit', 'applicationSha256', 'sourceKitSha256',
            'toolingKitSha256', 'originalRuntimeSha256', 'rebuiltRuntimeSha256',
            'binaryCache', 'loader', 'cli', 'originalPackageUnchanged')


def verify(record, entries, inputs):
    require(record.get('schema') == 1 and record.get('sourceCommit') == inputs['sourceCommit'],
            'Qualification source mismatch')
    qt, usb = record.get('qt', {}), record.get('usb', {})
    for result in (qt, usb):
        require(result.get('status') == 'pass' and result.get('originalPackageUnchanged') is True,
                'Replacement qualification incomplete')
        require(result.get('applicationSha256') == entries['SDR_Town.exe']['sha256'],
                'Qualification executable mismatch')
    require(qt.get('fullQtRebuildVerified') is True and qt.get('qtVersion') == inputs['qtVersion'],
            'Full Qt replacement not verified')
    require(qt.get('sourceKitSha256') == entries['licenses/qt/source-materials.zip']['sha256'],
            'Qualification Qt source mismatch')
    wanted_qt = {n for n in entries if component(n) == 'qt'}
    require(qt.get('originalQtSha256') == {n: entries[n]['sha256'] for n in wanted_qt},
            'Qualification Qt runtime mismatch')
    require(set(qt.get('rebuiltSha256', {})) == wanted_qt, 'Incomplete Qt replacement set')
    for key in ('applicationCli', 'nativeTlsBackend', 'rendererAndPlugins', 'widgetsAndLoopbackNetwork'):
        require(qt.get(key) == 'pass', 'Qt integration qualification failed: ' + key)
    require(qt.get('applicationGui') == 'four-no-rx-profiles-pass', 'Qt GUI qualification incomplete')
    require(usb.get('sourceCommit') == inputs['sourceCommit'] and usb.get('binaryCache') == 'disabled'
            and usb.get('loader') == 'pass' and usb.get('cli') == 'pass', 'USB qualification incomplete')
    for field, path in (('sourceKitSha256', 'licenses/vcpkg/source-materials.zip'),
                        ('toolingKitSha256', 'licenses/vcpkg/tooling-materials.zip')):
        require(usb.get(field) == entries[path]['sha256'], 'USB qualification source/tool mismatch')
    require(usb.get('originalRuntimeSha256') == {n: entries[n]['sha256'] for n in USB},
            'USB qualification runtime mismatch')
    require(set(usb.get('rebuiltRuntimeSha256', {})) == set(USB), 'Incomplete USB replacement set')
    for values in (qt['rebuiltSha256'], usb['rebuiltRuntimeSha256']):
        require(all(isinstance(h, str) and re.fullmatch('[0-9a-f]{64}', h) for h in values.values()),
                'Invalid replacement digest')
    return record


def export(stage, qt_report, usb_report):
    files = tree_files(stage)
    entries = {n: {'sha256': sha256(p)} for n, p in files.items()}
    inputs = parse_json((stage / 'licenses/build-inputs.json').read_bytes())
    qt, usb = parse_json(qt_report.read_bytes()), parse_json(usb_report.read_bytes())
    record = {'schema': 1, 'sourceCommit': inputs['sourceCommit'],
              'qt': {k: qt[k] for k in QT_KEYS}, 'usb': {k: usb[k] for k in USB_KEYS}}
    verify(record, entries, inputs)
    path = stage / QUALIFICATION
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(json_bytes(record))
    print('PASS exact-package Qt/USB qualification')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--qt-report', type=Path, required=True)
    parser.add_argument('--usb-report', type=Path, required=True)
    args = parser.parse_args()
    export(args.stage, args.qt_report, args.usb_report)
