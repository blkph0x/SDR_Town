from pathlib import Path
root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(errors='replace')
rxh = (root / 'include' / 'Receiver.h').read_text(errors='replace')
dmh = (root / 'include' / 'DeviceManager.h').read_text(errors='replace')
dmc = (root / 'src' / 'DeviceManager.cpp').read_text(errors='replace')
assert 'uint64_t p25TrafficGeneration = 0;' in rxh
assert 'std::atomic<uint64_t> p25TrafficSourceGeneration' in main
assert 'fetch_add(1, std::memory_order_acq_rel)' in main
assert 'rx.p25TrafficGeneration == 0 || rx.p25TrafficGeneration != liveGen' in main
assert 'setReceiverCursorBeforeLiveEdge' in dmh
assert 'void DeviceManager::setReceiverCursorBeforeLiveEdge' in dmc
assert 'p25Phase2TrafficPreRollSamples(source.sampleRateHz, source.retunesPrimary)' in main
assert 'setReceiverCursorBeforeLiveEdge(\n                    source.deviceIndex, *trafficRx,' in main
assert 'setReceiverCursorToLiveEdge(source.deviceIndex, *trafficRx)' not in main
assert 'oldTraffic->active = false;' in main
print('P25 traffic-source generation/pre-roll regression: PASS')
