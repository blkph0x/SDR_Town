#include "Receiver.h"

// Currently mostly header-only for simplicity.
// Future: move heavier receiver-owned services here (recorders, schedulers, scanner state).
// Per-receiver audio routing now lives in Receiver::audioOutputIndices and AudioEngine::pushAudioToOutputs().
