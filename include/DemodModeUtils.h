#pragma once

// Purpose: Demod mode string helpers, voice diag labels, and built-in band plans.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase A (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include "Demod.h"
#include "P25VoiceDecode.h"

#include <QString>

#include <string>
#include <vector>

std::string trimCopy(const std::string& s);
std::string modeToString(DemodMode mode);
QString modeToQString(DemodMode mode);
DemodMode modeFromString(std::string text);
const char* p25VoiceDiagLabel(P25VoiceDiagCode code);
