#pragma once

// Purpose: P25 live decoder configs, control-offset probe, and CLI decode report.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase A (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include "P25Control.h"
#include "P25LiveDecoder.h"
#include "P25VoiceDecode.h"
#include "Receiver.h"

#include <complex>
#include <string>
#include <vector>

P25LiveDecoderConfig p25DiagnosticDecoderConfig();
P25LiveDecoderConfig p25RealtimeControlDecoderConfig();
P25LiveDecoderConfig p25CliControlGrantDecoderConfig();
P25LiveDecoderConfig p25VoiceDecoderConfig(bool phase2,
                                           P25VoiceDecodeProfile profile = P25VoiceDecodeProfile::Realtime);
P25LiveDecoderConfig p25VoiceDecoderConfigForReceiver(const Receiver& rx,
                                                      P25VoiceDecodeProfile profile = P25VoiceDecodeProfile::Realtime);
int p25CliDecodeScore(const P25LiveDecodeResult& result);
bool p25ControlDecodeHasTrustedPayload(const P25LiveDecodeResult& result);
bool p25ControlDecodeHasValidatedNid(const P25LiveDecodeResult& result);
bool p25ControlDecodeShouldProbeOffsets(const P25LiveDecodeResult& result);
P25LiveDecodeResult decodeP25ControlWithOffsetProbe(P25LiveDecoder& decoder,
                                                    const std::vector<std::complex<float>>& iq,
                                                    double sampleRateHz,
                                                    double centerFreqHz,
                                                    double nominalTargetHz,
                                                    double* effectiveTargetHz = nullptr);
void p25SeedAnalyzerNacFromDecode(P25ControlChannelAnalyzer& analyzer,
                                  const P25LiveDecodeResult& result);
void printP25CliDecodeReport(const std::string& label,
                             int devIndex,
                             double centerFreqHz,
                             double sampleRateHz,
                             double targetHz,
                             const P25LiveDecodeResult& result,
                             P25ControlChannelAnalyzer& analyzer);
