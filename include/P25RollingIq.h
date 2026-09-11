#pragma once

// Purpose: Phase-2 rolling IQ window and live-edge pull helpers.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase 4 (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include "DeviceManager.h"
#include "Receiver.h"
#include "P25VoiceTiming.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

struct RollingIqWindow {
    std::vector<std::complex<float>> samples;
    uint64_t startAbsolute = 0;
    uint64_t endAbsolute = 0;
    uint64_t lastDecodeAbsolute = 0;
    uint64_t submittedDecodeEndAbsolute = 0;
    uint64_t heldDecodeStartAbsolute = 0;
    uint64_t heldDecodeEndAbsolute = 0;
    uint64_t streamEpoch = 0;
    bool absoluteKnown = false;
    bool decodeAbsoluteKnown = false;
    bool submittedDecodeEndKnown = false;
    bool heldDecodeRangeKnown = false;
    bool streamEpochKnown = false;

    void clear()
    {
        samples.clear();
        startAbsolute = 0;
        endAbsolute = 0;
        lastDecodeAbsolute = 0;
        submittedDecodeEndAbsolute = 0;
        heldDecodeStartAbsolute = 0;
        heldDecodeEndAbsolute = 0;
        streamEpoch = 0;
        absoluteKnown = false;
        decodeAbsoluteKnown = false;
        submittedDecodeEndKnown = false;
        heldDecodeRangeKnown = false;
        streamEpochKnown = false;
    }

    uint64_t effectiveDecodeAbsolute() const
    {
        if (!decodeAbsoluteKnown) return lastDecodeAbsolute;
        if (!submittedDecodeEndKnown) return lastDecodeAbsolute;
        return std::max(lastDecodeAbsolute, submittedDecodeEndAbsolute);
    }

    void markDecodeSubmitted(uint64_t decodeEndAbsolute)
    {
        // Stream-absolute path: need absoluteKnown.  Sample-index path (no
        // hardware absolute): still advance the submitted cursor so we never
        // re-decode the whole buffer every tick (multi-voice / echo symptom).
        if (absoluteKnown) {
            if (!decodeAbsoluteKnown) return;
            submittedDecodeEndAbsolute = decodeEndAbsolute;
            submittedDecodeEndKnown = true;
            return;
        }
        submittedDecodeEndAbsolute = decodeEndAbsolute;
        submittedDecodeEndKnown = true;
        decodeAbsoluteKnown = true;
    }

    void commitDecodeAbsolute(uint64_t decodeEndAbsolute)
    {
        if (absoluteKnown) {
            if (!decodeAbsoluteKnown || decodeEndAbsolute > lastDecodeAbsolute) {
                lastDecodeAbsolute = decodeEndAbsolute;
                decodeAbsoluteKnown = true;
            }
        } else {
            // Sample-index cursor: end offset into the rolling buffer.
            if (!decodeAbsoluteKnown || decodeEndAbsolute > lastDecodeAbsolute) {
                lastDecodeAbsolute = decodeEndAbsolute;
            }
            decodeAbsoluteKnown = true;
        }
        if (heldDecodeRangeKnown && decodeEndAbsolute >= heldDecodeEndAbsolute) {
            heldDecodeRangeKnown = false;
            heldDecodeStartAbsolute = 0;
            heldDecodeEndAbsolute = 0;
        }
        submittedDecodeEndKnown = false;
    }

    void rollbackSubmittedDecode()
    {
        submittedDecodeEndKnown = false;
    }

    void holdDecodeAbsolute(uint64_t decodeEndAbsolute)
    {
        heldDecodeStartAbsolute = lastDecodeAbsolute;
        heldDecodeEndAbsolute = decodeEndAbsolute;
        heldDecodeRangeKnown = decodeEndAbsolute > heldDecodeStartAbsolute;
        submittedDecodeEndKnown = false;
    }

    bool resultCoversHeldDecodeRange(uint64_t resultStartAbsolute,
                                     bool resultStartAbsoluteKnown,
                                     uint64_t resultEndAbsolute,
                                     bool resultEndAbsoluteKnown) const noexcept
    {
        if (!heldDecodeRangeKnown) return true;
        if (!resultEndAbsoluteKnown || resultEndAbsolute < heldDecodeEndAbsolute) return false;
        const bool comparableStartKnown = resultStartAbsoluteKnown || !absoluteKnown;
        if (!comparableStartKnown) return false;
        return resultStartAbsolute <= heldDecodeStartAbsolute;
    }

    bool append(const DeviceManager::RecentIQWindow& win, size_t maxSamples)
    {
        if (win.samples.empty()) return false;
        if (win.cursorDiscontinuity) {
            clear();
            return false;
        }
        if (win.streamEpoch != 0) {
            if (streamEpochKnown && streamEpoch != win.streamEpoch) {
                // Monotonic retune handoff: keep accumulated IQ/overlap; only
                // advance the epoch marker so callers can reset P25 timing if needed.
                lastDecodeAbsolute = startAbsolute;
                decodeAbsoluteKnown = absoluteKnown;
            }
            streamEpoch = win.streamEpoch;
            streamEpochKnown = true;
        }

        const bool validAbsolute =
            win.endAbsolute >= win.startAbsolute &&
            (win.endAbsolute - win.startAbsolute) == static_cast<uint64_t>(win.samples.size());

        // Never let a non-absolute / corrupt window wipe a live absolute stream.
        // Field 20260807: absKnown flipped mid-call → full-buffer re-decode →
        // multi-talker / echo / chop while oppVcw stayed 0 (same buffer replayed).
        if (!samples.empty() && absoluteKnown && !validAbsolute) {
            return false;
        }

        if (samples.empty()) {
            samples = win.samples;
            startAbsolute = validAbsolute ? win.startAbsolute : 0;
            endAbsolute = validAbsolute ? win.endAbsolute : static_cast<uint64_t>(samples.size());
            absoluteKnown = validAbsolute;
            lastDecodeAbsolute = startAbsolute;
            decodeAbsoluteKnown = validAbsolute || !samples.empty();
            if (!absoluteKnown) {
                // Sample-index mode: cursor at 0 so the first take is the full fill.
                lastDecodeAbsolute = 0;
            }
        } else if (!absoluteKnown && !validAbsolute) {
            // Stay in sample-index mode: append only.
            samples.insert(samples.end(), win.samples.begin(), win.samples.end());
            endAbsolute = static_cast<uint64_t>(samples.size());
        } else if (!absoluteKnown && validAbsolute) {
            // Promote to absolute stream once the device reports valid cursors.
            const size_t priorSize = samples.size();
            samples.insert(samples.end(), win.samples.begin(), win.samples.end());
            // Anchor so prior sample-index cursor maps into absolute space.
            startAbsolute = win.startAbsolute > priorSize
                ? win.startAbsolute - static_cast<uint64_t>(priorSize)
                : 0;
            endAbsolute = startAbsolute + static_cast<uint64_t>(samples.size());
            absoluteKnown = true;
            if (decodeAbsoluteKnown) {
                lastDecodeAbsolute = startAbsolute + std::min<uint64_t>(
                    lastDecodeAbsolute, static_cast<uint64_t>(samples.size()));
            } else {
                lastDecodeAbsolute = startAbsolute;
                decodeAbsoluteKnown = true;
            }
        } else if (win.endAbsolute <= startAbsolute) {
            return false;
        } else if (win.startAbsolute > endAbsolute) {
            // RX pull cursor raced ahead of the rolling decode buffer and returned
            // a disjoint absolute window.  Do not replace accumulated IQ/context;
            // stitch the new span onto the rolling buffer instead.
            const uint64_t gap = win.startAbsolute - endAbsolute;
            const uint64_t maxJoinGap = static_cast<uint64_t>(std::max<size_t>(4096, maxSamples / 8));
            if (gap > 0 && gap <= maxJoinGap) {
                samples.insert(samples.end(),
                    static_cast<size_t>(gap),
                    std::complex<float>(0.0f, 0.0f));
                endAbsolute += gap;
            }
            samples.insert(samples.end(), win.samples.begin(), win.samples.end());
            endAbsolute = win.endAbsolute;
        } else if (win.endAbsolute > endAbsolute) {
            const uint64_t overlap = endAbsolute > win.startAbsolute ? endAbsolute - win.startAbsolute : 0;
            if (overlap > win.samples.size()) {
                // Corrupt overlap (cursor jumped backward without discontinuity flag): replace.
                samples = win.samples;
                startAbsolute = win.startAbsolute;
                endAbsolute = win.endAbsolute;
                absoluteKnown = validAbsolute;
                lastDecodeAbsolute = startAbsolute;
                decodeAbsoluteKnown = validAbsolute;
            } else {
                const size_t firstNew = static_cast<size_t>(std::min<uint64_t>(overlap, win.samples.size()));
                samples.insert(samples.end(), win.samples.begin() + static_cast<std::ptrdiff_t>(firstNew), win.samples.end());
                endAbsolute = win.endAbsolute;
            }
        } else {
            return false;
        }

        if (maxSamples > 0 && samples.size() > maxSamples) {
            size_t drop = samples.size() - maxSamples;
            // sdrtrunk's traffic source never discards undecoded RF.  Field logs
            // showed a ~1 s absStart jump right after the first gate=emit: buffer
            // trim advanced startAbsolute and then bumped lastDecodeAbsolute to
            // match, permanently skipping the rest of the active voice call.
            if (absoluteKnown && decodeAbsoluteKnown && effectiveDecodeAbsolute() >= startAbsolute) {
                const uint64_t decodeCursor = effectiveDecodeAbsolute();
                const size_t decodeHeadSamples = static_cast<size_t>(
                    std::min<uint64_t>(decodeCursor - startAbsolute, samples.size()));
                // DEC-0023 / capture 20260908_095936: protect the DEC-0009
                // speaker-sustain overlap (280 ms), not 80 ms. Soft-trim that
                // only kept 80 ms clipped live eyes to fresh+80 = 160 ms
                // (110 hops) → wrong-slot / eye death after a good start.
                // 163840 @ 2.048 MHz = 80 ms; 573440 = 280 ms.
                constexpr size_t kProtectedOverlapSamples = 573440;
                if (decodeHeadSamples > kProtectedOverlapSamples) {
                    // Keep pre-roll before the decode cursor.  The old math used
                    // samples.size() - (decodeHead + overlap), which can drop
                    // past decodeCursor once the rolling buffer is full.  That
                    // turns overlapped Phase-2 traffic into context-free 160 ms
                    // islands and produces the live stutter/garble seen in
                    // 20260901_042425 / 20260908_095936 even though replay is
                    // gapless.
                    const size_t maxSafeDrop = decodeHeadSamples - kProtectedOverlapSamples;
                    drop = std::min(drop, maxSafeDrop);
                } else {
                    drop = 0;
                }
            }
            if (drop == 0) {
                // Soft trim refused because undecoded RF is protected.  When the
                // voice worker cannot keep up, refuse unbounded growth (capture
                // 080701 Follow#2 rolling ballooned to ~64MB / ~31s of lag).
                // DEC-0023 / 20260908_095936: do **not** jump lastDecodeAbsolute
                // to the buffer head — that skipped the rest of the call and
                // left only 80 ms pre-roll (160 ms eyes). Only drop already-
                // decoded prefix; if still over hardCap, allow growth up to an
                // emergency ceiling (SDRTrunk never discards undecoded RF).
                const size_t hardCap = maxSamples + (maxSamples / 2);
                const size_t emergencyCap = maxSamples * 4; // 4× active rolling
                if (samples.size() > hardCap) {
                    size_t emergencyDrop = 0;
                    if (absoluteKnown && decodeAbsoluteKnown &&
                        effectiveDecodeAbsolute() >= startAbsolute) {
                        const uint64_t decodeCursor = effectiveDecodeAbsolute();
                        const size_t decodeHeadSamples = static_cast<size_t>(
                            std::min<uint64_t>(decodeCursor - startAbsolute, samples.size()));
                        constexpr size_t kProtectedOverlapSamples = 573440;
                        if (decodeHeadSamples > kProtectedOverlapSamples) {
                            emergencyDrop = decodeHeadSamples - kProtectedOverlapSamples;
                        }
                    }
                    if (emergencyDrop > 0) {
                        samples.erase(samples.begin(),
                                      samples.begin() + static_cast<std::ptrdiff_t>(emergencyDrop));
                        if (absoluteKnown) {
                            startAbsolute += static_cast<uint64_t>(emergencyDrop);
                        } else if (decodeAbsoluteKnown) {
                            if (lastDecodeAbsolute > emergencyDrop) {
                                lastDecodeAbsolute -= static_cast<uint64_t>(emergencyDrop);
                            } else {
                                lastDecodeAbsolute = 0;
                            }
                            if (submittedDecodeEndKnown) {
                                if (submittedDecodeEndAbsolute > emergencyDrop) {
                                    submittedDecodeEndAbsolute -= static_cast<uint64_t>(emergencyDrop);
                                } else {
                                    submittedDecodeEndAbsolute = 0;
                                }
                            }
                            endAbsolute = static_cast<uint64_t>(samples.size());
                        }
                    } else if (samples.size() > emergencyCap) {
                        // Last resort only: still never invent PCM; drop oldest
                        // and advance the cursor so RAM cannot grow without bound.
                        const size_t forceDrop = samples.size() - maxSamples;
                        samples.erase(samples.begin(),
                                      samples.begin() + static_cast<std::ptrdiff_t>(forceDrop));
                        if (absoluteKnown) {
                            startAbsolute += static_cast<uint64_t>(forceDrop);
                            if (!decodeAbsoluteKnown || lastDecodeAbsolute < startAbsolute) {
                                lastDecodeAbsolute = startAbsolute;
                                decodeAbsoluteKnown = true;
                            }
                        } else if (decodeAbsoluteKnown) {
                            if (lastDecodeAbsolute > forceDrop) {
                                lastDecodeAbsolute -= static_cast<uint64_t>(forceDrop);
                            } else {
                                lastDecodeAbsolute = 0;
                            }
                        }
                        endAbsolute = absoluteKnown
                            ? endAbsolute
                            : static_cast<uint64_t>(samples.size());
                    }
                }
                return true;
            }
            samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(drop));
            if (absoluteKnown) {
                startAbsolute += static_cast<uint64_t>(drop);
                if (decodeAbsoluteKnown && lastDecodeAbsolute < startAbsolute) {
                    lastDecodeAbsolute = startAbsolute;
                }
                if (submittedDecodeEndKnown && submittedDecodeEndAbsolute < startAbsolute) {
                    submittedDecodeEndAbsolute = startAbsolute;
                }
            } else if (decodeAbsoluteKnown) {
                if (lastDecodeAbsolute > drop) lastDecodeAbsolute -= static_cast<uint64_t>(drop);
                else lastDecodeAbsolute = 0;
                if (submittedDecodeEndKnown) {
                    if (submittedDecodeEndAbsolute > drop) {
                        submittedDecodeEndAbsolute -= static_cast<uint64_t>(drop);
                    } else {
                        submittedDecodeEndAbsolute = 0;
                    }
                }
                endAbsolute = static_cast<uint64_t>(samples.size());
            }
        }
        return true;
    }

    std::vector<std::complex<float>> takeUndecoded(size_t maxSamples,
                                                    size_t overlapSamples,
                                                    uint64_t& outStartAbsolute,
                                                    bool& outAbsoluteKnown,
                                                    size_t* outFreshSamples = nullptr,
                                                    size_t* outContextSamples = nullptr,
                                                    size_t minFreshSamples = 0,
                                                    uint64_t* outDecodeEndAbsolute = nullptr,
                                                    bool* outDecodeEndAbsoluteKnown = nullptr,
                                                    bool allowPartialFresh = true)
    {
        outStartAbsolute = 0;
        outAbsoluteKnown = false;
        if (outFreshSamples) *outFreshSamples = 0;
        if (outContextSamples) *outContextSamples = 0;
        if (outDecodeEndAbsolute) *outDecodeEndAbsolute = 0;
        if (outDecodeEndAbsoluteKnown) *outDecodeEndAbsoluteKnown = false;
        if (samples.empty()) return {};

        const uint64_t decodeCursor = effectiveDecodeAbsolute();
        size_t firstNew = 0;
        if (absoluteKnown && decodeAbsoluteKnown) {
            if (decodeCursor >= endAbsolute) return {};
            if (decodeCursor > startAbsolute) {
                firstNew = static_cast<size_t>(std::min<uint64_t>(decodeCursor - startAbsolute, samples.size()));
            }
        } else if (!absoluteKnown && decodeAbsoluteKnown) {
            // Sample-index cursor: lastDecode/submitted are offsets into samples[].
            firstNew = static_cast<size_t>(std::min<uint64_t>(decodeCursor, static_cast<uint64_t>(samples.size())));
        }

        // Capture 20260808_010625: absKnown=no + waiting-fresh for 15s+ while
        // rolling grew to 4–6M samples. Cursor sat at the live edge (firstNew
        // ≈ size) after hard-cap catch-up; recover by rewinding into the buffer
        // so minFresh can be satisfied from recent RF (abs-dedupe drops replays).
        if (!absoluteKnown && firstNew >= samples.size() && samples.size() > minFreshSamples) {
            const size_t recoverFresh = std::max(minFreshSamples, static_cast<size_t>(8192));
            if (samples.size() > recoverFresh) {
                firstNew = samples.size() - recoverFresh;
                lastDecodeAbsolute = static_cast<uint64_t>(firstNew);
                decodeAbsoluteKnown = true;
                submittedDecodeEndKnown = false;
            }
        }

        if (firstNew >= samples.size()) return {};
        // Soften minFresh when we only have a partial live-edge fill so we do
        // not sit in waiting-fresh while the worker is free (010625: minFresh
        // 245760 with empty take while RF was arriving).
        size_t effectiveMinFresh = minFreshSamples;
        if (samples.size() > firstNew) {
            const size_t available = samples.size() - firstNew;
            if (allowPartialFresh && effectiveMinFresh > 0 && available > 0 && available < effectiveMinFresh) {
                const size_t softFloor = absoluteKnown ? static_cast<size_t>(4096) : static_cast<size_t>(8192);
                if (available >= softFloor) {
                    effectiveMinFresh = available;
                }
            }
        }

        // Phase 2 bursts are only 180 dibits apart and the symbol/timing recovery
        // needs pre-roll to stay locked.  Decoding a strictly non-overlapped
        // 120-160 ms tail produced exactly the field symptom we saw: one good
        // 4V/AMBE burst followed by NID/no-sync until the next lucky alignment.
        // Include controlled pre-roll before the first not-yet-decoded sample,
        // but advance lastDecodeAbsolute to the true stream end.  The Phase-2
        // AMBE emitter uses absolute dibit de-duplication, so overlap supplies
        // timing context without replaying already-emitted voice frames.
        size_t first = firstNew;
        if (overlapSamples > 0) {
            first = (firstNew > overlapSamples) ? (firstNew - overlapSamples) : 0;
        }

        // Cap fresh tail length while preserving overlap pre-roll.  Capping the
        // total returned span at maxSamples alone dropped context=0 windows after
        // the first lucky Phase-2 sync hit and starved subsequent p2bursts.
        const size_t returnedEnd = (maxSamples > 0)
            ? std::min(samples.size(), firstNew + maxSamples)
            : samples.size();
        const size_t count = returnedEnd > first ? returnedEnd - first : 0;
        if (count == 0) return {};
        const size_t freshBegin = std::max(first, firstNew);
        const size_t freshSamples = returnedEnd > freshBegin ? returnedEnd - freshBegin : 0;
        const size_t contextSamples = freshBegin > first ? freshBegin - first : 0;
        if (effectiveMinFresh > 0 && freshSamples < effectiveMinFresh) {
            return {};
        }
        // After the first Phase-2 lock, never emit a sustain chunk without overlap
        // pre-roll; context=0 windows were the field signature for rolling collapse.
        if (decodeAbsoluteKnown && overlapSamples > 0 && firstNew > 0 && contextSamples == 0) {
            return {};
        }
        if (outFreshSamples) *outFreshSamples = freshSamples;
        if (outContextSamples) *outContextSamples = contextSamples;

        std::vector<std::complex<float>> out(
            samples.begin() + static_cast<std::ptrdiff_t>(first),
            samples.begin() + static_cast<std::ptrdiff_t>(returnedEnd));

        if (absoluteKnown) {
            outStartAbsolute = startAbsolute + static_cast<uint64_t>(first);
            outAbsoluteKnown = true;
            if (outDecodeEndAbsolute) {
                *outDecodeEndAbsolute = startAbsolute + static_cast<uint64_t>(returnedEnd);
            }
            if (outDecodeEndAbsoluteKnown) {
                *outDecodeEndAbsoluteKnown = true;
            }
        } else {
            // Sample-index mode still reports a stable end cursor for submit/commit.
            outStartAbsolute = static_cast<uint64_t>(first);
            outAbsoluteKnown = false;
            if (outDecodeEndAbsolute) {
                *outDecodeEndAbsolute = static_cast<uint64_t>(returnedEnd);
            }
            if (outDecodeEndAbsoluteKnown) {
                *outDecodeEndAbsoluteKnown = true;
            }
        }
        return out;
    }
};

size_t p25Phase2UndecodedBacklogSamples(const RollingIqWindow& rolling) noexcept;

void p25Phase2PrepareRollingIqPull(DeviceManager& mgr,
                                   size_t devIndex,
                                   Receiver& rx,
                                   RollingIqWindow& rolling,
                                   size_t rollingWindow,
                                   size_t& pullWindow,
                                   double sampleRateHz);
