"""DEC-0185: exercise independent session addressing in the actual GUI, no RF."""
import argparse
import json
from pathlib import Path
import secrets
import socket
import subprocess
import time
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=Path('build/bin/Release/SDR_Town.exe'))
    parser.add_argument('--output', type=Path, default=Path('build/sstv-session-qa'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    token = secrets.token_hex(24)
    url = f'http://127.0.0.1:{port}'

    def request(path, body=None):
        req = urllib.request.Request(url + path, data=None if body is None else json.dumps(body).encode(),
                                     headers={'Authorization': 'Bearer '+token, 'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(req, timeout=5) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            return json.load(error)

    command = [str(args.exe.resolve()), '--allow-multiple', '--no-remote-diagnostics',
               '--gui-dry-run', '--control-port', str(port), '--control-token', token,
               '--control-auth-required', '--gui-exit-after-ms', '16000']
    startup = None
    if hasattr(subprocess, 'STARTUPINFO'):
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    with (args.output/'gui.log').open('w', encoding='utf-8') as log:
        proc = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, startupinfo=startup)
        try:
            deadline = time.monotonic()+12
            while True:
                if proc.poll() is not None:
                    raise RuntimeError(f'GUI exited: {proc.returncode}')
                try:
                    if request('/v1/health')['ok']:
                        break
                except (OSError,KeyError):
                    pass
                if time.monotonic() >= deadline:
                    raise TimeoutError('Session GUI did not start')
                time.sleep(.1)
            for name in ('qa_radio_a','qa_radio_b','QA_RADIO_A'):
                assert request('/v1/sstv/sessions', {'sessionId':name})['ok']
            sessions = request('/v1/sstv/sessions')['sessions']
            assert {s['sessionId'] for s in sessions} == {'qa_radio_a','qa_radio_b'}, sessions
            assert len(sessions)==2 and all(not s['busy'] for s in sessions), sessions
            for path in ('sessions','live','finish','cancel'):
                for invalid in ('../escape', 'x'*65, 12):
                    result = request('/v1/sstv/'+path, {'sessionId':invalid})
                    assert not result['ok'] and result['status']==400, result
            for path in ('finish','cancel'):
                assert not request('/v1/sstv/'+path, {'sessionId':'not_open'})['ok']
            assert len(request('/v1/sstv/sessions')['sessions'])==2
            (args.output/'result.json').write_text(json.dumps({'ok':True,'sessions':sessions},indent=2),encoding='utf-8')
            proc.wait(timeout=25)
            assert proc.returncode==0, proc.returncode
        finally:
            if proc.poll() is None:
                proc.kill(); proc.wait(timeout=10)
    output=(args.output/'gui.log').read_text(encoding='utf-8',errors='replace')
    for forbidden in ('Background: Attempting Soapy make','Started real Soapy streaming','STUB/no-hardware IQ mode'):
        assert forbidden not in output, forbidden
    print('PASS: two distinct GUI sessions, idempotent open, invalid/stale commands rejected, no RX')


if __name__ == '__main__':
    main()
