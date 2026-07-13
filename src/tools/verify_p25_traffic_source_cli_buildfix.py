#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')

assert 'std::atomic<uint64_t> p25TrafficSourceGeneration{0};' in main, 'GUI/member traffic generation counter missing'
assert 'std::atomic<uint64_t> cliP25TrafficSourceGeneration{0};' in main, 'CLI local traffic generation counter missing'

# The actual CLI function is the second int runCLI occurrence (first can be a prototype).
positions = []
start = 0
while True:
    idx = main.find('int runCLI(', start)
    if idx < 0:
        break
    positions.append(idx)
    start = idx + 1
assert len(positions) >= 2, 'runCLI definition not found'
cli_start = positions[-1]
cli_end = main.find('    auto printStats =', cli_start)
assert cli_end > cli_start, 'CLI monitor region not found'
cli_region = main[cli_start:cli_end]
pre_region = main[:cli_start]

assert 'p25TrafficSourceGeneration.load(std::memory_order_acquire)' in pre_region, 'GUI/member generation loads were accidentally changed'
assert 'cliP25TrafficSourceGeneration.load(std::memory_order_acquire)' in cli_region, 'CLI stale-generation loads must use local counter'
assert 'p25TrafficSourceGeneration.load(std::memory_order_acquire)' not in cli_region, 'CLI still references GUI member generation counter'

print('P25 traffic-source CLI buildfix regression: PASS')
