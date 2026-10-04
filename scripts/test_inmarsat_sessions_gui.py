"""DEC-0187: real GUI session lifecycle/control isolation, strictly no RF."""
import argparse
import json
from pathlib import Path
import secrets
import socket
import subprocess
import time
import urllib.request
import urllib.error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=Path('build/bin/Release/SDR_Town.exe'))
    parser.add_argument('--output', type=Path, default=Path('build/inmarsat-session-qa'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0)); port = probe.getsockname()[1]
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

    def command(action, session='qa_inmarsat_a', **extra):
        return request('/v1/inmarsat/sessions', {'action': action, 'sessionId': session, **extra})

    startup = None
    if hasattr(subprocess, 'STARTUPINFO'):
        startup = subprocess.STARTUPINFO(); startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    with (args.output/'gui.log').open('w', encoding='utf-8') as log:
        proc = subprocess.Popen([str(args.exe.resolve()), '--allow-multiple', '--no-remote-diagnostics',
            '--gui-dry-run', '--control-port', str(port), '--control-token', token,
            '--control-auth-required', '--gui-exit-after-ms', '22000'],
            stdout=log, stderr=subprocess.STDOUT, startupinfo=startup)
        try:
            deadline = time.monotonic()+15
            while True:
                if proc.poll() is not None:
                    raise RuntimeError(f'GUI exited: {proc.returncode}')
                try:
                    if request('/v1/health')['ok']: break
                except OSError: pass
                if time.monotonic() >= deadline: raise TimeoutError('GUI did not start')
                time.sleep(.1)
            original = request('/v1/inmarsat/status')['inmarsat']['config']
            for name in ('qa_inmarsat_a', 'qa_inmarsat_b', 'QA_INMARSAT_A'):
                assert command('open', name)['ok']
            for name, freq, rate in (('qa_inmarsat_a',1546005000,10500),('qa_inmarsat_b',1542935000,8400)):
                assert command('configure', name, config={'deviceStableKey':'missing-'+name,
                    'channelHz':freq,'baud':rate,'playAudio':False})['ok']
            states = request('/v1/inmarsat/sessions')['sessions']
            selected = {s['sessionId']:s for s in states if s['sessionId'].startswith('qa_inmarsat_')}
            assert len(selected)==2
            assert selected['qa_inmarsat_a']['config']['channelHz']==1546005000
            assert selected['qa_inmarsat_b']['config']['channelHz']==1542935000
            for bad in ('../escape','bad\n',12,'x'*65):
                result = command('open', bad)
                assert not result['ok'] and result['status']==400
            for action in ('stop','close','configure','start'):
                assert command(action, 'does-not-exist')['status']==409
            assert command(12)['status']==400
            for config in ({'channelHz':-1},{'baud':1234},{'baud':1200.5},{'baud':1e100},{'playAudio':'wrong'},{'unknown':True}):
                assert command('configure', config=config)['status']==400
            assert command('start')['status']==409 # Dry-run cannot silently open hardware.
            assert command('stop')['ok']
            assert command('close')['ok']
            remaining = request('/v1/inmarsat/sessions')['sessions']
            assert any(s['sessionId']=='qa_inmarsat_b' and s['config']['channelHz']==1542935000 for s in remaining)
            assert not any(s['sessionId']=='qa_inmarsat_a' for s in remaining)
            assert command('open')['ok'] # Settings survive controller destruction.
            assert any(s['sessionId']=='qa_inmarsat_a' and s['config']['channelHz']==1546005000
                       for s in request('/v1/inmarsat/sessions')['sessions'])
            assert request('/v1/inmarsat/status')['inmarsat']['config']==original
            assert command('close')['ok']; assert command('close','qa_inmarsat_b')['ok']
            proc.wait(timeout=30); assert proc.returncode==0
        finally:
            if proc.poll() is None: proc.kill(); proc.wait(timeout=10)
    text = (args.output/'gui.log').read_text(encoding='utf-8',errors='replace')
    for forbidden in ('Background: Attempting Soapy make','Started real Soapy streaming','STUB/no-hardware IQ mode'):
        assert forbidden not in text, forbidden
    (args.output/'result.json').write_text(json.dumps({'ok':True,'sessions':selected},indent=2),encoding='utf-8')
    print('PASS: independent Inmarsat GUI engines/configuration, reopen, rejected stale/invalid commands and no RF')


if __name__ == '__main__': main()
