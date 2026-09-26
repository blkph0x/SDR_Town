"""Offline regression gates for DEC-0132's published, dated channel surveys."""
import argparse
from collections import Counter
import json
from pathlib import Path


def check(data_dir):
    expected = {
        '4f2': {600: 14, 1200: 1, 8400: 12, 10500: 5},
        '4f3': {600: 9, 1200: 1, 8400: 9, 10500: 4},
        '3f5': {600: 4, 8400: 47, 10500: 2},
        'i4a': {600: 2, 1200: 1, 8400: 6, 10500: 2},
        '6f1': {600: 6, 10500: 4},
    }
    modes = {600: 'aero_msk', 1200: 'aero_msk', 8400: 'aero_voice', 10500: 'aero_oqpsk'}
    plans = {}
    for name, counts in expected.items():
        plan = json.loads((data_dir / (name + '.json')).read_text(encoding='utf-8'))
        assert plan['id'] == name
        assert '828314e0512604879df6828ee5cbda2d02f9ea6d' in plan['source']
        assert 'not a live 2026 survey' in plan['source']
        rows = plan['channels']
        assert Counter(row['baud'] for row in rows) == counts, (name, Counter(row['baud'] for row in rows))
        assert len({row['freqHz'] for row in rows}) == len(rows), name
        for row in rows:
            assert isinstance(row['freqHz'], int) and 1525000000 <= row['freqHz'] <= 1559000000
            assert row['mode'] == modes[row['baud']]
            assert f"{row['freqHz'] / 1e6:.4f}" in row['label']
        plans[name] = {row['freqHz']: row['baud'] for row in rows}
    # Independent samples from the source table, including the old wrong-rate case.
    for plan, hz, rate in [('4f2', 1542935000, 8400), ('4f2', 1545070000, 1200),
                           ('4f2', 1546005000, 10500), ('4f3', 1546062500, 10500),
                           ('3f5', 1545460000, None), ('i4a', 1546142500, 8400),
                           ('6f1', 1545076800, 600), ('6f1', 1546036100, 10500)]:
        assert plans[plan].get(hz) == rate, (plan, hz, rate)
    assert 1543085000 not in plans['4f2']  # Former interpolated fake P2.
    assert 1544500000 not in plans['4f2']  # Former unsupported voice default.
    assert 8400 not in plans['6f1'].values()  # Source explicitly retires old list.
    print('PASS: all five dated Aero surveys, rate/mode mapping, exact Hz and removed invented defaults')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--data-dir', type=Path, default=Path(__file__).resolve().parents[1] / 'data/inmarsat')
    check(parser.parse_args().data_dir)
