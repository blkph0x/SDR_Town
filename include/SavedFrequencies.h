#pragma once

// Purpose: Saved frequency persistence and GUI table population.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase A (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include "CliApp.h"

#include <QTableWidget>

#include <vector>

std::vector<SavedFrequency> loadSavedFrequencies();
void saveSavedFrequencies(const std::vector<SavedFrequency>& freqs);
void populateSavedFrequencyTable(QTableWidget* table, const std::vector<SavedFrequency>& freqs);
