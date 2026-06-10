#include "Receiver.h"

// Currently mostly header-only for simplicity.
// Future: move any heavy methods here (e.g. recorder logic, schedule processing).

// Stub for the per-receiver audio routing idea.
// For now the existing global AudioEngine is used; this will be wired in Phase 1.
// void pushReceiverAudio(Receiver& rx, const float* samples, size_t count) {
//     // if (rx.audioOutputIndices.empty()) { global push } else { push only to rx targets }
// }
