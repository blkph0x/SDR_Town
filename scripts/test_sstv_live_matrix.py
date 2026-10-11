"""DEC-0217: analog mode matrix through the production RF router, then Auto image decode.

Synthetic encoder/decoder round trips are not independent on-air qualification.
test_sstv_rf.py supplies the independent recorded oracle; test_sstv_virtual_sdr.py
supplies actual wall-clock Soapy/live-worker coverage. No RF is transmitted.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

import numpy as np
from scipy.signal import resample_poly


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--modes', nargs='*')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--snr-db', type=float, default=50.97, help='IQ signal/noise power ratio before filtering')
    parser.add_argument('--deviation', type=float, default=2500, help='FM deviation Hz at full-scale input')
    parser.add_argument('--clock-ppm', type=int, default=0, help='Transmitter audio clock error')
    parser.add_argument('--doppler-hz', type=float, default=0, help='Synthetic approach/recede sweep, +Hz to -Hz; receive correction updates once a second')
    args = parser.parse_args()
    if not np.isfinite(args.snr_db) or not -10 <= args.snr_db <= 80 or not 1000 <= args.deviation <= 5000 or abs(args.clock_ppm)>1000 or not 0 <= args.doppler_hz <= 15000:
        raise ValueError('Invalid RF fixture impairment')
    root = Path(__file__).resolve().parents[1]
    binary = root / 'build/bin/Release'
    helper = root / 'build/sstv-backend/release/sdrtown_sstv.exe'
    modes = args.modes or re.findall(r'\{"([a-z0-9]+)",', (root/'include/SstvModes.h').read_text())
    modes = [m for m in modes if m != 'hamdrm']
    args.out.mkdir(parents=True, exist_ok=False)
    results = []
    for mode in modes:
        with tempfile.TemporaryDirectory(prefix='sstv-matrix-', dir=root/'build') as directory:
            tmp = Path(directory)
            made = subprocess.run([str(helper), '--test-signal', mode, str(tmp/'source.pcm')],
                                  capture_output=True, text=True, check=True, timeout=30)
            info = json.loads(made.stdout)
            source = np.fromfile(tmp/'source.pcm', dtype='<i2').astype(float)/32768
            tx = resample_poly(source, 1000000, 1000000+args.clock_ppm) if args.clock_ppm else source
            # One second idle, finite SNR and a brief leader fade; preserve VIS bits.
            audio = np.concatenate((np.zeros(48000), tx))
            audio = resample_poly(audio, 2, 1)
            iq = (0.5*np.exp(1j*np.cumsum(2*np.pi*args.deviation*audio/96000))).astype('complex64')
            if args.doppler_hz:
                time = np.arange(len(iq), dtype=float)/96000
                duration = len(iq)/96000
                iq *= np.exp(2j*np.pi*args.doppler_hz*(time-time*time/duration)).astype('complex64')
                del time
            if mode != 'fax480':
                iq[round(1.080*96000):round(1.090*96000)] = 0
            rng = np.random.default_rng(217)
            noise = 0.5/(np.sqrt(2)*10**(args.snr_db/20))
            iq += (noise*(rng.normal(size=len(iq))+1j*rng.normal(size=len(iq)))).astype('complex64')
            iq.astype('<c8').tofile(tmp/'iq.cf32')
            del iq, audio
            env = dict(os.environ, SDR_TOWN_SSTV_RF_IQ=str(tmp/'iq.cf32'),
                       SDR_TOWN_SSTV_RF_PCM=str(tmp/'demod.pcm'), SDR_TOWN_SSTV_RF_EXPECT='NFM',
                       SDR_TOWN_SSTV_RF_REQUEST='NFM' if mode == 'fax480' else 'auto')
            env['SDR_TOWN_SSTV_RF_DOPPLER_HZ'] = str(args.doppler_hz)
            rf = subprocess.run([str(binary/'sdr_town_tests.exe'), '[sstv-rf-recording]'],
                                env=env, capture_output=True, text=True, timeout=180)
            result = {'mode': mode, 'rfAuto': mode != 'fax480', 'rfPassed': rf.returncode == 0,
                      'snrDb': args.snr_db, 'deviationHz': args.deviation, 'clockPpm': args.clock_ppm,
                      'dopplerHz': args.doppler_hz}
            log = rf.stdout + rf.stderr
            if rf.returncode == 0:
                decoded = subprocess.run([str(helper), str(tmp/'demod.pcm'), '48000', str(tmp/'images'), 'auto'],
                                         capture_output=True, text=True, timeout=120)
                log += decoded.stdout + decoded.stderr
                items = [json.loads(line) for line in decoded.stdout.splitlines() if line.startswith('{')]
                complete = [i for i in items if i.get('complete')]
                result['images'] = items
                result['passed'] = decoded.returncode == 0 and len(complete) == 1 and complete[0]['mode'] == mode
                if result['passed']:
                    item = complete[0]
                    got = np.fromfile(tmp/'images'/item['file'], dtype=np.uint8).reshape(info['height'], info['width'], 3)
                    # Compare RF output against clean decoded PCM, keeping codec's
                    # grayscale conversion and color-space rounding as the oracle.
                    reference = subprocess.run([str(helper), str(tmp/'source.pcm'), '48000', str(tmp/'ref'), mode],
                                               capture_output=True, text=True, check=True, timeout=120)
                    refitem = json.loads(reference.stdout.splitlines()[0])
                    assert refitem['complete'], refitem
                    ref = np.fromfile(tmp/'ref'/refitem['file'], dtype=np.uint8).reshape(got.shape)
                    result['rgbMae'] = float(np.abs(got.astype(float)-ref).mean())
                    result['passed'] = result['rgbMae'] < 12.75  # Existing DEC-0117 5% pixel budget.
            else:
                result['passed'] = False
            (args.out/(mode+'.log')).write_text(log, encoding='utf-8')
            results.append(result)
            (args.out/'results.json').write_text(json.dumps(results, indent=2))
            print(json.dumps(result), flush=True)
    assert all(r['passed'] for r in results), 'matrix failures: '+str([r['mode'] for r in results if not r['passed']])


if __name__ == '__main__':
    main()
