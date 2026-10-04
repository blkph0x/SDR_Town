"""DEC-0187/0189: real GUI session lifecycle/control isolation, strictly no RF."""
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
            '--control-auth-required', '--gui-exit-after-ms', '35000'],
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
            # DEC-0189: exercise the real satellite and aircraft session routes,
            # not just the underlying ownership policy or a mock HTTP handler.
            for workflow in ('satcom', 'aircraft'):
                path = f'/v1/{workflow}/sessions'
                def operation(action, name='qa_a', **extra):
                    return request(path, {'action':action,'sessionId':name,**extra})
                def named_states():
                    return {s['sessionId']:s for s in request(path)['sessions'] if s['sessionId']}
                for name in ('qa_a','qa_b','QA_A'):
                    assert operation('open',name)['ok']
                for bad in ('bad\n','../escape','x'*65,12):
                    assert operation('open',bad)['status']==400
                for action in ('stop','close','configure'):
                    assert operation(action,'not-open')['status']==409
                assert operation(99)['status']==400
                if workflow == 'satcom':
                    for name,freq in (('qa_a',145800000),('qa_b',137100000)):
                        assert operation('configure',name,config={'deviceStableKey':'missing-'+name,
                            'lowHz':freq,'highHz':freq,'monitorAudio':False})['ok']
                    initial = named_states()
                    for config in ({'lowHz':-1},{'stepHz':0},{'lowHz':'bad'},{'mode':'invalid'}, {'unknown':True}):
                        assert operation('configure',config=config)['status']==400
                    assert named_states()['qa_a']['config']==initial['qa_a']['config']
                    assert operation('start')['status']==409
                else:
                    for name,bw in (('qa_a',4),('qa_b',8)):
                        assert operation('configure',name,deviceKey='missing-'+name,captureBandwidthMHz=bw)['ok']
                    initial = named_states()
                    for bw in (-1,'bad',1e100):
                        assert operation('configure',captureBandwidthMHz=bw,deviceKey='must-not-apply')['status']==400
                    assert named_states()['qa_a']['deviceKey']=='missing-qa_a'
                    assert operation('tune')['status']==409
                    assert operation('local',enabled=True)['ok']
                    assert not named_states()['qa_b']['localDecodeEnabled']
                assert operation('stop')['ok']
                assert operation('close')['ok']
                assert 'qa_a' not in named_states() and 'qa_b' in named_states()
                assert operation('open')['ok']
                reopened = named_states()['qa_a']
                if workflow == 'satcom':
                    assert reopened['config']['lowHz']==145800000 and not reopened['passArmed']
                else:
                    assert reopened['deviceKey']=='missing-qa_a' and reopened['captureBandwidthMHz']==4, reopened
                    assert not reopened['radioBusy']
                assert operation('close')['ok']; assert operation('close','qa_b')['ok']
            proc.wait(timeout=45); assert proc.returncode==0
        finally:
            if proc.poll() is None: proc.kill(); proc.wait(timeout=10)
    text = (args.output/'gui.log').read_text(encoding='utf-8',errors='replace')
    for forbidden in ('Background: Attempting Soapy make','Started real Soapy streaming','STUB/no-hardware IQ mode'):
        assert forbidden not in text, forbidden
    (args.output/'result.json').write_text(json.dumps({'ok':True,
        'verifiedWorkflows':['inmarsat','satcom','aircraft'],'inmarsatSessions':selected},indent=2),encoding='utf-8')
    print('PASS: independent Inmarsat, Satcom and Aircraft GUI controllers, reopen, rejected stale/invalid commands and no RF')


if __name__ == '__main__': main()
