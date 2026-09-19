#include "MainWindow.h"
#include "RepeaterControlHooks.h"
#include "RepeaterMonitor.h"

// AUTOMOC: Q_OBJECT lives in MainWindow.h.
// ISS-0010: live GUI P25 decode pipeline (voice worker + guiDspWorker rolling IQ).
// Mechanical extract from MainWindow::MainWindow — no hop/feed/CADENCE changes.

void MainWindow::startP25LiveDecodePipeline()
{
        // Dedicated background DSP worker thread for the GUI monitor path.
        // Owned (not detached) so we can join on shutdown. Uses stop flag.
        startP25VoiceWorker();
        stopDspWorker.store(false, std::memory_order_release);
        guiDspWorker = std::thread([this]() {
            auto& mgr = DeviceManager::instance();
            std::map<ReceiverSessionKey, std::chrono::steady_clock::time_point> lastPhase2DecodeByRx;
            P25SpeakerPendingMap pendingAudioByRx;
            std::map<ReceiverSessionKey, RollingIqWindow> phase2IqByRx;
            uint64_t lastTrafficGenerationSeen = 0;
            uint64_t lastPendingAudioFlushSeq = 0;
            std::deque<MainWindow::P25VoiceDecodeResult> pendingVoicePublishResults;
            auto drainP25VoiceResults = [&]() {
                for (auto& result : takeP25VoiceDecodeResults()) {
                    pendingVoicePublishResults.push_back(std::move(result));
                    p25VoicePendingPublishDepth.store(pendingVoicePublishResults.size(),
                        std::memory_order_release);
                }
                auto purgeLocalPublishResultsForSession =
                    [&](const ReceiverSessionKey& sessionKey, uint64_t afterSequence) -> size_t {
                    size_t purged = 0;
                    for (auto it = std::next(pendingVoicePublishResults.begin());
                         it != pendingVoicePublishResults.end();) {
                        if (it->receiverSessionKey == sessionKey &&
                            (afterSequence == 0 || it->sequence > afterSequence)) {
                            it = pendingVoicePublishResults.erase(it);
                            ++purged;
                        } else {
                            ++it;
                        }
                    }
                    if (purged > 0) {
                        p25VoiceDroppedResults.fetch_add(purged, std::memory_order_relaxed);
                        p25VoicePendingPublishDepth.store(pendingVoicePublishResults.size(),
                            std::memory_order_release);
                    }
                    return purged;
                };
                bool drained = false;
                while (!pendingVoicePublishResults.empty()) {
                    auto& result = pendingVoicePublishResults.front();
                    if (result.rx) {
                        std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                        if (!lk.owns_lock()) {
                            p25VoicePublicationLockMisses.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                        bool rxStillValid = false;
                        for (auto& r : receivers) {
                            if (r && r.get() == result.rx.get()) {
                                rxStillValid = true;
                                break;
                            }
                        }
                        if (!rxStillValid) {
                            if (result.rx && result.rollingDecode && result.iqDecodeEndAbsoluteKnown) {
                                auto rollingIt = phase2IqByRx.find(result.receiverSessionKey);
                                if (rollingIt != phase2IqByRx.end()) {
                                    rollingIt->second.rollbackSubmittedDecode();
                                }
                            }
                            pendingVoicePublishResults.pop_front();
                            p25VoicePendingPublishDepth.store(pendingVoicePublishResults.size(),
                                std::memory_order_release);
                            drained = true;
                            continue;
                        }
                    }
                    if (result.rx && result.rollingDecode && result.iqDecodeEndAbsoluteKnown) {
                        auto rollingIt = phase2IqByRx.find(result.receiverSessionKey);
                        if (rollingIt != phase2IqByRx.end() &&
                            !rollingIt->second.resultCoversHeldDecodeRange(
                                result.iqStartAbsolute,
                                result.iqStartAbsoluteKnown,
                                result.iqDecodeEndAbsolute,
                                result.iqDecodeEndAbsoluteKnown)) {
                            rollingIt->second.rollbackSubmittedDecode();
                            const size_t localPurged = purgeLocalPublishResultsForSession(
                                result.receiverSessionKey, result.sequence);
                            const MainWindow::P25VoiceDecodeWorkPurge workerPurged =
                                purgeP25VoiceDecodeWorkForSession(result.receiverSessionKey,
                                    result.sequence);
                            const uintptr_t rxKey = reinterpret_cast<uintptr_t>(result.rx.get());
                            const uint64_t seqLog = result.sequence;
                            const qulonglong localPurgedLog =
                                static_cast<qulonglong>(localPurged);
                            const qulonglong workerJobsPurgedLog =
                                static_cast<qulonglong>(workerPurged.pendingJobs);
                            const qulonglong workerResultsPurgedLog =
                                static_cast<qulonglong>(workerPurged.completedResults);
                            QTimer::singleShot(0, this, [this, rxKey, seqLog, localPurgedLog,
                                                          workerJobsPurgedLog,
                                                          workerResultsPurgedLog]() {
                                appendP25LogLineKeyed(
                                    QString("p25-rolling-held-result-drop:%1").arg(static_cast<qulonglong>(rxKey)),
                                    QString("P25 rolling held-result drop: seq=%1 did not cover held selected-slot RF; purged publish=%2 jobs=%3 completed=%4.")
                                        .arg(static_cast<qulonglong>(seqLog))
                                        .arg(localPurgedLog)
                                        .arg(workerJobsPurgedLog)
                                        .arg(workerResultsPurgedLog),
                                    750);
                            });
                            pendingVoicePublishResults.pop_front();
                            p25VoicePendingPublishDepth.store(pendingVoicePublishResults.size(),
                                std::memory_order_release);
                            drained = true;
                            continue;
                        }
                    }
                    const P25VoicePublishOutcome outcome =
                        publishP25VoiceDecodeResult(result, pendingAudioByRx);
                    if (outcome == P25VoicePublishOutcome::Deferred) {
                        break;
                    }
                    if (result.rx && result.rollingDecode && result.iqDecodeEndAbsoluteKnown) {
                        auto rollingIt = phase2IqByRx.find(result.receiverSessionKey);
                        if (rollingIt != phase2IqByRx.end()) {
                            const bool consumedRollingWindow =
                                outcome == P25VoicePublishOutcome::Published &&
                                result.hasAudioBlock &&
                                p25Phase2RollingDecodeWindowConsumed(result.audio);
                            if (outcome == P25VoicePublishOutcome::Published &&
                                result.hasAudioBlock &&
                                consumedRollingWindow) {
                                rollingIt->second.commitDecodeAbsolute(result.iqDecodeEndAbsolute);
                            } else if (outcome == P25VoicePublishOutcome::DiscardedStale ||
                                       outcome == P25VoicePublishOutcome::ReceiverGone ||
                                       (outcome == P25VoicePublishOutcome::Published &&
                                         !result.hasAudioBlock)) {
                                rollingIt->second.rollbackSubmittedDecode();
                            } else if (outcome == P25VoicePublishOutcome::Published &&
                                       result.hasAudioBlock &&
                                       !consumedRollingWindow) {
                                // DEC-0037: hold the rolling range for a MAC/ESS
                                // retry, but do NOT purge newer publish/jobs.
                                // Capture 095846 TG 30302: hold+purge turned one
                                // unqueued eye into a silent follow. Keep later
                                // near-live work; overlap will still re-see this RF.
                                rollingIt->second.holdDecodeAbsolute(result.iqDecodeEndAbsolute);
                                const uintptr_t rxKey = reinterpret_cast<uintptr_t>(result.rx.get());
                                const uint64_t seqLog = result.sequence;
                                const qulonglong targetVcwLog = static_cast<qulonglong>(result.audio.phase2TargetVoiceCodewords);
                                const qulonglong expVcwLog = static_cast<qulonglong>(result.audio.phase2ExpectedVoiceCodewords);
                                const qulonglong fedLog = static_cast<qulonglong>(result.audio.phase2FedToMbelib);
                                const qulonglong queuedLog = static_cast<qulonglong>(result.audio.phase2PendingAmbeFramesQueued);
                                const qulonglong ctxDropLog = static_cast<qulonglong>(result.audio.phase2ContextSuppressedVoiceCodewords);
                                const qulonglong dupLog = static_cast<qulonglong>(result.audio.phase2DuplicateSuppressedVoiceCodewords);
                                const qulonglong absDupLog = static_cast<qulonglong>(result.audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords);
                                const qulonglong seqDropLog = static_cast<qulonglong>(result.audio.phase2SequencerSuppressedVoiceCodewords);
                                const qulonglong rejectLog = static_cast<qulonglong>(result.audio.phase2RejectedVoiceCodewords);
                                const qulonglong wrongSlotLog = static_cast<qulonglong>(result.audio.phase2WrongSlotVoiceCodewords);
                                QTimer::singleShot(0, this, [this, rxKey, seqLog, targetVcwLog,
                                                             expVcwLog, fedLog, queuedLog,
                                                             ctxDropLog, dupLog, absDupLog,
                                                             seqDropLog, rejectLog, wrongSlotLog]() {
                                    appendP25LogLineKeyed(QString("p25-rolling-cursor-hold:%1").arg(static_cast<qulonglong>(rxKey)),
                                        QString("P25 rolling cursor hold: seq=%1 targetVcw=%2 expVcw=%3 fed=%4 pendingQueued=%5 ctxDrop=%6 dup=%7 absDup=%8 seqDrop=%9 reject=%10 wrongSlot=%11; selected voice not consumed, retrying with later MAC/ESS/context (no purge).")
                                            .arg(static_cast<qulonglong>(seqLog))
                                            .arg(targetVcwLog)
                                            .arg(expVcwLog)
                                            .arg(fedLog)
                                            .arg(queuedLog)
                                            .arg(ctxDropLog)
                                            .arg(dupLog)
                                            .arg(absDupLog)
                                            .arg(seqDropLog)
                                            .arg(rejectLog)
                                            .arg(wrongSlotLog),
                                        750);
                                });
                            }
                        }
                    }
                    if (outcome == P25VoicePublishOutcome::Published &&
                        result.rx && result.hasAudioBlock &&
                        (p25Phase2ShouldFlushStaleVoicePipeline(result.audio) ||
                         p25Phase2ShouldFlushAudioTail(result.audio))) {
                        Receiver& rx = *result.rx;
                        phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                        p25Phase2ClearSpeakerPendingQueue(rx,
                                                          p25SpeakerPendingFor(pendingAudioByRx, rx),
                                                          P25PendingClearReason::RetuneOrGeneration);
                        lastPhase2DecodeByRx.erase(p25ReceiverSessionKey(rx));
                        size_t devIndex = 0;
                        bool haveDev = false;
                        {
                            std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
                            if (rxLock.owns_lock() && rx.active) {
                                devIndex = rx.deviceIndex;
                                haveDev = true;
                            }
                        }
                        if (haveDev) {
                            DeviceManager::instance().setReceiverCursorToLiveEdge(devIndex, rx);
                        }
                    }
                    pendingVoicePublishResults.pop_front();
                    p25VoicePendingPublishDepth.store(pendingVoicePublishResults.size(),
                        std::memory_order_release);
                    drained = true;
                }
                return drained;
            };
            while (!stopDspWorker.load(std::memory_order_acquire)) {
                bool didWork = false;
                for (int drainPass = 0; drainPass < 6; ++drainPass) {
                    if (!drainP25VoiceResults()) break;
                    didWork = true;
                }
                const uint64_t flushSeq = p25PendingAudioFlushSeq.load(std::memory_order_acquire);
                if (flushSeq != lastPendingAudioFlushSeq) {
                    pendingAudioByRx.clear();
                    phase2IqByRx.clear();
                    lastPhase2DecodeByRx.clear();
                    lastPendingAudioFlushSeq = flushSeq;
                }
                const uint64_t liveTrafficGeneration =
                    p25TrafficSourceGeneration.load(std::memory_order_acquire);
                if (liveTrafficGeneration != 0 && liveTrafficGeneration != lastTrafficGenerationSeen) {
                    pendingAudioByRx.clear();
                    phase2IqByRx.clear();
                    lastPhase2DecodeByRx.clear();
                    lastTrafficGenerationSeen = liveTrafficGeneration;
                }
                // S0-2 (P0 audit): short lock to snapshot the current receivers (shared_ptrs — cheap, stable).
                // Then process without holding lock. shared_ptr keeps the Demodulator alive even if vector reallocates.
                std::vector<std::shared_ptr<Receiver>> rxSnapshot;
                {
                    // try_to_lock: never stall the DSP worker behind a GUI grant path
                    // that (historically) held receiversMutex while waiting on stateMutex.
                    std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                    if (lk.owns_lock()) {
                        ensureReceiver();
                        rxSnapshot.reserve(receivers.size());
                        for (auto& r : receivers) if (r && r->active) rxSnapshot.push_back(r);
                    }
                }
                if (rxSnapshot.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                auto receiverSessionStillActive = [&rxSnapshot](const ReceiverSessionKey& key) {
                    return std::any_of(rxSnapshot.begin(), rxSnapshot.end(),
                        [&key](const std::shared_ptr<Receiver>& r) {
                            return r.get() == key.receiver &&
                                r->p25TrafficSessionGeneration.load(std::memory_order_acquire) == key.generation;
                        });
                };
                for (auto it = lastPhase2DecodeByRx.begin(); it != lastPhase2DecodeByRx.end();) {
                    it = receiverSessionStillActive(it->first) ? std::next(it) : lastPhase2DecodeByRx.erase(it);
                }
                for (auto it = pendingAudioByRx.begin(); it != pendingAudioByRx.end();) {
                    it = receiverSessionStillActive(it->first) ? std::next(it) : pendingAudioByRx.erase(it);
                }
                for (auto it = phase2IqByRx.begin(); it != phase2IqByRx.end();) {
                    it = receiverSessionStillActive(it->first) ? std::next(it) : phase2IqByRx.erase(it);
                }
                for (size_t r = 0; r < rxSnapshot.size() && !stopDspWorker.load(std::memory_order_acquire); ++r) {
                    auto& rxPtr = rxSnapshot[r];
                    if (!rxPtr) continue;
                    Receiver& rx = *rxPtr;  // reference to the stable object
                    size_t i = 0;
                    bool rxP25VoiceDecodeSnapshot = false;
                    bool rxP25VoicePhase2Snapshot = false;
                    bool skipPausedOneRtlControlReceiver = false;

                    {
                        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                        if (!rx.active) continue;
                        i = rx.deviceIndex;
                        rxP25VoiceDecodeSnapshot = rx.p25VoiceDecodeEnabled;
                        rxP25VoicePhase2Snapshot = rx.p25VoicePhase2;
                        skipPausedOneRtlControlReceiver =
                            p25IndependentTrafficActive &&
                            p25IndependentTrafficRetunedPrimary &&
                            rx.p25ControlChannelMute &&
                            !rx.p25VoiceDecodeEnabled &&
                            !rx.p25IndependentTrafficSource;
                    }
                    auto logP25VoiceSchedulerEarly = [&](const QString& reason,
                                                         const QString& detail,
                                                         int throttleMs = 1000) {
                        const uintptr_t rxKey = reinterpret_cast<uintptr_t>(&rx);
                        QTimer::singleShot(0, this, [this, rxKey, reason, detail, throttleMs]() {
                            appendP25LogLineKeyed(QString("p25-voice-scheduler:%1:%2")
                                    .arg(static_cast<qulonglong>(rxKey))
                                    .arg(reason),
                                QString("P25 voice scheduler: %1 %2").arg(reason, detail),
                                throttleMs);
                        });
                    };
                    if (skipPausedOneRtlControlReceiver) {
                        mgr.setReceiverCursorToLiveEdge(i, rx);
                        didWork = true;
                        continue;
                    }
                    if (i >= mgr.getDevices().size() || !mgr.isStreaming(i)) {
                        if (rxP25VoiceDecodeSnapshot && rxP25VoicePhase2Snapshot) {
                            logP25VoiceSchedulerEarly("stream-not-ready",
                                QString("device=%1 valid=%2 streaming=%3; voice follow is armed but no IQ can be pulled.")
                                    .arg(i)
                                    .arg(i < mgr.getDevices().size() ? "yes" : "no")
                                    .arg((i < mgr.getDevices().size() && mgr.isStreaming(i)) ? "yes" : "no"),
                                1500);
                        }
                        continue;
                    }

                    std::vector<float> pwr; double cf = 0.0, sr = 0.0;
                    if (!mgr.getLatestSpectrum(i, pwr, cf, sr) || sr <= 0.0) {
                        bool canUseVoiceFallback = false;
                        double fallbackCenterHz = 0.0;
                        {
                            std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                            canUseVoiceFallback = rx.active && rx.deviceIndex == i && rx.p25VoiceDecodeEnabled;
                            fallbackCenterHz = p25Phase2TrafficSourceCenterHz(rx);
                        }
                        if (!canUseVoiceFallback) {
                            if (rxP25VoiceDecodeSnapshot && rxP25VoicePhase2Snapshot) {
                                logP25VoiceSchedulerEarly("no-spectrum-or-fallback",
                                    QString("device=%1; latest spectrum unavailable and voice fallback is not armed.")
                                        .arg(i),
                                    1500);
                            }
                            continue;
                        }
                        const auto devices = mgr.getDevices();
                        if (i < devices.size()) sr = devices[i].sampleRate;
                        if (!std::isfinite(sr) || sr <= 0.0) sr = 2.048e6;
                        cf = (std::isfinite(fallbackCenterHz) && fallbackCenterHz > 0.0)
                            ? fallbackCenterHz
                            : 100e6;
                        pwr.clear();
                    }

                    std::vector<float> ch;
                    double rms = -100;
                    long long dspMicros = 0;
                    double afcOffsetHz = 0.0;
                    RfSquelchMetrics rfMetrics;
                    std::vector<size_t> rxAudioOutputs;
                    double monFreq = 100e6, monLpf = 15000.0, monSquelch = -80.0;
                    double monGain = 1.0, monWfmDe = 75.0, monWfmNotch = 0.96, monBw = 180000.0;
                    double demodFreq = 100e6;
                    double rfSquelchLevel = std::numeric_limits<double>::quiet_NaN();
                    DemodMode monMode = DemodMode::AUTO;
                    bool monAudioLpfEnabled = true;
                    bool monP25ControlMute = false;
                    bool monP25VoiceDecode = false;
                    bool monP25VoicePhase2 = false;
                    bool monP25IndependentTrafficSource = false;
                    uint64_t monP25TrafficGeneration = 0;
                    uint32_t monP25VoiceTalkgroupId = 0;
                    uint32_t monP25VoiceSourceId = 0;
                    bool monP25VoiceTdmaSlotKnown = false;
                    uint8_t monP25VoiceTdmaSlot = 0;
                    double monP25TrafficVoiceFreqHz = 0.0;
                    bool skipP25VoiceWindow = false;
                    bool p25VoiceOutputMutedForSettle = false;
                    bool appliedQueuedSlotProbe = false;
                    uint8_t appliedQueuedSlot = 0;
                    bool appliedQueuedVoiceReset = false;
                    bool phase2BufferedDecode = false;
                    bool phase2SustainDecodeWindow = false;
                    bool phase2SessionHadVoiceLock = false;
                    bool phase2SessionHadBurstEye = false;
                    bool phase2SessionSpeakerSustain = false;
                    bool phase2StableSuperframeLock = false;
                    bool phase2WideReacquireWindow = false;
                    bool phase2MaskEpochRepairWindow = false;
                    bool phase2EstablishedClearStreaming = false;
                    bool phase2HardTargetAcquire = false;
                    bool phase2UseRecentTrafficWindow = false;
                    bool phase2VoiceQueueSustainHint = false;
                    uint64_t iqStartAbsolute = 0;
                    bool iqStartAbsoluteKnown = false;
                    uint64_t rdsStreamEpoch = 0, rdsIqStart = 0;
                    bool rdsSourceGap = false;
                    uint64_t iqDecodeEndAbsolute = 0;
                    bool iqDecodeEndAbsoluteKnown = false;
                    size_t phase2FreshIqSamples = 0;
                    size_t phase2ContextIqSamples = 0;
                    std::vector<std::complex<float>> iq;
                    AudioEngine* audioOutputEngine = nullptr;
                    size_t audioActiveOutputCount = 0;
                    size_t audioQueuedSamples = 0;
                    double audioRingFillPercent = 0.0;
                    int audioUnderrunCount = 0;

                    auto logP25VoiceScheduler = [&](const QString& reason,
                                                     const QString& detail,
                                                     int throttleMs = 1000) {
                        const uintptr_t rxKey = reinterpret_cast<uintptr_t>(&rx);
                        QTimer::singleShot(0, this, [this, rxKey, reason, detail, throttleMs]() {
                            appendP25LogLineKeyed(QString("p25-voice-scheduler:%1:%2")
                                    .arg(static_cast<qulonglong>(rxKey))
                                    .arg(reason),
                                QString("P25 voice scheduler: %1 %2").arg(reason, detail),
                                throttleMs);
                        });
                    };

                    {
                        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                        if (!rx.active || rx.deviceIndex != i) continue;
                        if (rx.p25IndependentTrafficSource) {
                            const uint64_t liveGen = p25TrafficSourceGeneration.load(std::memory_order_acquire);
                            if (rx.p25TrafficGeneration == 0 || rx.p25TrafficGeneration != liveGen) {
                                rx.active = false;
                                rx.p25VoiceDecodeEnabled = false;
                                continue;
                            }
                        }
                        monFreq = rx.freqHz;
                        monMode = rx.mode;
                        monBw = rx.channelBwHz;
                        monLpf = rx.lpfHz;
                        monAudioLpfEnabled = rx.audioLpfEnabled;
                        monSquelch = rx.squelchDb;
                        monGain = rx.audioGain;
                        monWfmDe = rx.wfmDeTauUs;
                        monWfmNotch = rx.wfmPilotNotchR;
                        monP25ControlMute = rx.p25ControlChannelMute;
                        monP25VoiceDecode = rx.p25VoiceDecodeEnabled;
                        monP25VoicePhase2 = rx.p25VoicePhase2;
                        monP25IndependentTrafficSource = rx.p25IndependentTrafficSource;
                        monP25TrafficGeneration = rx.p25TrafficGeneration;
                        monP25VoiceTalkgroupId = rx.p25VoiceTalkgroupId;
                        monP25VoiceSourceId = rx.p25VoiceSourceId;
                        monP25VoiceTdmaSlotKnown = rx.p25VoiceTdmaSlotKnown;
                        monP25VoiceTdmaSlot = static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u);
                        monP25TrafficVoiceFreqHz = rx.p25TrafficVoiceFreqHz > 0.0 ? rx.p25TrafficVoiceFreqHz : rx.freqHz;
                        phase2SustainDecodeWindow = p25Phase2UseSustainDecodeWindowLocked(rx);
                        phase2SessionHadVoiceLock = p25Phase2SessionHadVoiceLock(rx);
                        phase2SessionHadBurstEye = p25Phase2SessionHadBurstEye(rx);
                        phase2SessionSpeakerSustain = p25Phase2SessionSpeakerSustainActive(rx);
                        phase2StableSuperframeLock = p25Phase2HasStableSuperframeLockLocked(rx);
                        phase2WideReacquireWindow = p25Phase2NeedsWideReacquireWindowLocked(rx);
                        phase2MaskEpochRepairWindow = rx.p25Phase2MaskEpochRepairHoldWindows > 0;
                        phase2EstablishedClearStreaming = p25Phase2EstablishedClearVoiceStreamingLocked(rx);
                        phase2HardTargetAcquire = p25Phase2SessionHasHardTargetAcquire(rx);
                        if (monP25VoiceDecode && rx.p25IndependentTrafficSource && rx.p25TrafficRetunesPrimary) {
                            const double trafficCenterHz = p25Phase2TrafficSourceCenterHz(rx);
                            if (std::isfinite(trafficCenterHz) && trafficCenterHz > 0.0) {
                                // Spectrum metadata can lag one-RTL retunes by a UI/DSP tick.
                                // The IQ ring after the retune is centered on the traffic source
                                // RF, which can be a low-IF center distinct from the granted voice.
                                cf = trafficCenterHz;
                            }
                        }
                        if (rx.p25VoiceResetPending) {
                            if (tryApplyP25VoiceResetLocked(rx)) {
                                monP25VoiceDecode = rx.p25VoiceDecodeEnabled;
                                monP25VoicePhase2 = rx.p25VoicePhase2;
                                monP25VoiceSourceId = rx.p25VoiceSourceId;
                                phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                                p25Phase2ClearSpeakerPendingQueue(rx,
                                                          p25SpeakerPendingFor(pendingAudioByRx, rx),
                                                          P25PendingClearReason::RetuneOrGeneration);
                                if (AudioEngine* eng = peekAudioEngineIfReady()) {
                                    eng->clearBuffers();
                                }
                                appliedQueuedVoiceReset = true;
                            }
                        }
                        if (rx.p25Phase2SpeakerPlaybackClearPending) {
                            p25Phase2ClearSpeakerPendingQueue(rx,
                                                      p25SpeakerPendingFor(pendingAudioByRx, rx),
                                                      P25PendingClearReason::RetuneOrGeneration);
                            rx.p25Phase2SpeakerPlaybackClearPending = false;
                            if (AudioEngine* eng = peekAudioEngineIfReady()) {
                                eng->clearBuffers();
                            }
                            const uint32_t tgLog = rx.p25VoiceTalkgroupId;
                            QTimer::singleShot(0, this, [this, tgLog]() {
                                appendP25LogLineKeyed(
                                    QString("p25-speaker-playback-cleared:%1").arg(tgLog),
                                    QString("P25 speaker playback cleared after same-call hop: TG=%1 pending+ring flushed.")
                                        .arg(tgLog),
                                    1500);
                            });
                        }
                        if (rx.p25VoiceSlotProbePending && rx.p25VoiceDecodeEnabled && rx.p25VoicePhase2) {
                            if (p25Phase2GrantedSlotIsImmutable(rx)) {
                                ++rx.p25DiagSlotProbeBlocked;
                                rx.p25VoiceSlotProbePending = false;
                                rx.p25VoiceSlotProbeRequested = 0;
                            } else {
                                std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                                if (dspLock.owns_lock()) {
                                    appliedQueuedSlot =
                                        static_cast<uint8_t>(rx.p25VoiceSlotProbeRequested & 0x01u);
                                    const bool probeApplied = applyP25Phase2SlotProbeLocked(
                                        rx, appliedQueuedSlot, QDateTime::currentMSecsSinceEpoch());
                                    monP25VoiceDecode = rx.p25VoiceDecodeEnabled;
                                    monP25VoicePhase2 = rx.p25VoicePhase2;
                                    monP25VoiceSourceId = rx.p25VoiceSourceId;
                                    if (probeApplied) {
                                        skipP25VoiceWindow = true;
                                        mgr.setReceiverCursorToLiveEdge(i, rx);
                                        phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                                        p25Phase2ClearSpeakerPendingQueue(rx,
                                                          p25SpeakerPendingFor(pendingAudioByRx, rx),
                                                          P25PendingClearReason::RetuneOrGeneration);
                                        appliedQueuedSlotProbe = true;
                                    }
                                }
                            }
                        }
                        rxAudioOutputs = rx.audioOutputIndices;
                        if (monP25VoiceDecode) {
                            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                            p25VoiceOutputMutedForSettle = rx.p25VoiceSettleUntilMs > nowMs;
                            // sdrtrunk starts the traffic-channel decoder immediately and
                            // gates speaker audio until clear/encrypted state is known. Do
                            // not skip Phase-2 decode during retune/settle; the first
                            // hundreds of milliseconds often contain the PTT/MAC/ESS that
                            // late entry needs.  Only explicit slot-probe resets set
                            // skipP25VoiceWindow above.
                            if (!p25VoiceOutputMutedForSettle && rx.p25VoiceDiscardWindows > 0) {
                                rx.p25VoiceDiscardWindows = 0;
                            }
                        }
                        if (!monP25VoiceDecode && monMode == DemodMode::AUTO && !pwr.empty()) {
                            auto smart = chooseSmartModeAndBandwidth(pwr, sr, cf, monFreq, DemodMode::AUTO, nullptr);
                            monMode = smart.mode;
                            monBw = smart.bandwidthHz;
                            monLpf = smart.lpfHz;
                        }
                        // Guard: NFM with a leftover WFM bandwidth sounds robotic.
                        if (monMode == DemodMode::NFM && monBw > 25000.0) {
                            monBw = 12500.0;
                            monLpf = std::min(monLpf, 3000.0);
                        }

                        // Probe RF level at the nominal tune first so strong local HT
                        // can disable spectrum-AFC (AFC hunt → warble/robotic audio).
                        rfMetrics = computeRfSquelchMetrics(pwr, sr, cf, monFreq, monBw, monMode);
                        const bool skipAnalogAfc = !monP25VoiceDecode && rfMetrics.valid
                            && rfMetrics.signalLevelDb > -45.0;
                        demodFreq = monP25VoiceDecode
                            ? p25VoiceAfcTargetHz(rx, p25Phase2VoiceSchedulerNominalHz(rx), monBw)
                            : (skipAnalogAfc
                                ? monFreq
                                : applyNfmAfcFromSpectrum(rx, pwr, sr, cf, monFreq, monBw, monMode));
                        // P25 TDMA acquisition must stay centered on the granted channel.
                        const double voiceNominalHz = monP25VoiceDecode
                            ? p25Phase2VoiceSchedulerNominalHz(rx)
                            : monFreq;
                        afcOffsetHz = demodFreq - voiceNominalHz;

                        rfMetrics = computeRfSquelchMetrics(pwr, sr, cf, demodFreq, monBw, monMode);
                        rfSquelchLevel = rfMetrics.valid
                            ? rfMetrics.signalLevelDb
                            : std::numeric_limits<double>::quiet_NaN();

                        // audit-followup-2: use per-rx cursor consumption. Each rx pulls only its own *new* chronological samples.
                        // No more "demod the latest 25ms overlapping window" for every rx.
                        phase2BufferedDecode = monP25VoiceDecode && monP25VoicePhase2;
                        // Traffic receivers have private absolute-sample cursors.  Use
                        // those cursors for Phase 2 too; polling the newest recent ring
                        // window can replay pre-retune control-channel IQ and make the
                        // GUI follow path look alive while the TDMA decoder is starved.
                        phase2UseRecentTrafficWindow = false;
                        if (appliedQueuedVoiceReset) {
                            logP25VoiceScheduler("reset-applied",
                                QString("cleared rolling IQ and skipped this DSP tick for tg=%1 voice=%2MHz.")
                                    .arg(monP25VoiceTalkgroupId)
                                    .arg(monP25TrafficVoiceFreqHz / 1e6, 0, 'f', 5),
                                1000);
                            didWork = true;
                            continue;
                        }
                        if (skipP25VoiceWindow) {
                            if (appliedQueuedSlotProbe) {
                                const int slotForLog = static_cast<int>(appliedQueuedSlot);
                                QTimer::singleShot(0, this, [this, slotForLog]() {
                                    appendP25LogLineKeyed(QString("tdma-slot-probe-applied:%1").arg(slotForLog),
                                        QString("TDMA slot auto-probe applied by DSP worker: selected slot %1; decoder history reset and audio remains gated until MAC/ESS lock.")
                                            .arg(slotForLog),
                                        2500);
                                });
                            }
                            logP25VoiceScheduler("slot-probe-applied",
                                QString("slot=%1; cleared rolling IQ and skipped this DSP tick.")
                                    .arg(static_cast<int>(appliedQueuedSlot)),
                                1000);
                            phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                            didWork = true;
                            continue;
                        }
                    } // release stateMutex before ring/IQ work — holding it across
                      // getNewIQWindow/takeUndecoded freezes GUI auto-follow (lock_guard).

                    if (!phase2BufferedDecode) {
                        phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                    }
                    {
                        size_t tgt = (sr > 0)
                            ? static_cast<size_t>(sr * (phase2BufferedDecode ? kP25Phase2VoiceDecodeWindowSeconds : 0.025))
                            : 8192;
                        if (phase2BufferedDecode) {
                            // Phase 2 traffic must be decoded from a continuous chronological
                            // stream.  The previous implementation sampled the newest recent
                            // 512 ms snapshot and then reset the receiver cursor to live edge on
                            // every decode cadence.  Field logs showed the consequence: the
                            // voice receiver was on the granted frequency with decode=yes, but
                            // every TDMA DEEP DIAG line stayed at block=no-vcw-from-live-window
                            // because most of the active call was skipped between snapshots.
                            // Keep a rolling per-receiver IQ window instead, append only new
                            // samples, and decode the rolling buffer at cadence.  This is much
                            // closer to sdrtrunk's continuously-running traffic-channel source.
                            const double rollingWindowSeconds =
                                (phase2SessionSpeakerSustain ||
                                 phase2EstablishedClearStreaming ||
                                 phase2SessionHadVoiceLock)
                                    ? kP25Phase2VoiceDecodeActiveRollingSeconds
                                    : kP25Phase2VoiceDecodeWindowSeconds;
                            const size_t rollingWindow = (sr > 0.0)
                                ? p25Phase2VoiceRollingMaxSamples(sr, rollingWindowSeconds)
                                : tgt;
                            const ReceiverSessionKey sessionKey = p25ReceiverSessionKey(rx);
                            auto& rolling = phase2IqByRx[sessionKey];
                            size_t pullWindow = (sr > 0.0)
                                ? static_cast<size_t>(std::clamp(sr * kP25Phase2VoicePullWindowSeconds, 4096.0, static_cast<double>(rollingWindow)))
                                : std::min<size_t>(tgt, rollingWindow);
                            p25Phase2PrepareRollingIqPull(mgr, i, rx, rolling, rollingWindow, pullWindow, sr);
                            auto newWin = phase2UseRecentTrafficWindow
                                ? mgr.getRecentIQWindowWithCursor(i, rollingWindow)
                                : mgr.getNewIQWindowForReceiver(i, rx, pullWindow);
                            if (newWin.streamEpoch != 0 && rolling.streamEpochKnown &&
                                rolling.streamEpoch != newWin.streamEpoch) {
                                if (newWin.samples.empty()) {
                                    newWin.cursorDiscontinuity = true;
                                } else {
                                    const uint64_t oldStreamEpoch = rolling.streamEpoch;
                                    const uint64_t newStreamEpoch = newWin.streamEpoch;
                                    logP25VoiceScheduler("stream-retune-handoff",
                                        QString("epoch=%1->%2 new=%3; clearing rolling IQ and restarting traffic decoder.")
                                            .arg(static_cast<qulonglong>(oldStreamEpoch))
                                            .arg(static_cast<qulonglong>(newStreamEpoch))
                                            .arg(static_cast<qulonglong>(newWin.samples.size())),
                                        1000);
                                    rolling.clear();
                                    p25Phase2ClearSpeakerPendingQueue(rx,
                                        pendingAudioByRx[sessionKey],
                                        P25PendingClearReason::RetuneOrGeneration);
                                    lastPhase2DecodeByRx.erase(sessionKey);
                                    bool resetQueuedOrApplied = false;
                                    {
                                        std::unique_lock<std::mutex> resetLock(rx.stateMutex, std::try_to_lock);
                                        if (resetLock.owns_lock()) {
                                            resetQueuedOrApplied =
                                                tryResetP25TrafficSessionNonBlocking(rx, "iq-stream-retune-handoff", true);
                                        } else {
                                            logP25VoiceScheduler("stream-retune-reset-deferred",
                                                QString("epoch=%1->%2; rx state busy, scheduler will retry without blocking.")
                                                    .arg(static_cast<qulonglong>(oldStreamEpoch))
                                                    .arg(static_cast<qulonglong>(newStreamEpoch)),
                                                1000);
                                        }
                                    }
                                    if (!resetQueuedOrApplied) {
                                        phase2IqByRx.erase(sessionKey);
                                    }
                                    didWork = true;
                                    continue;
                                }
                            }
                            if (newWin.cursorDiscontinuity) {
                                logP25VoiceScheduler("cursor-discontinuity",
                                    QString("new=%1 rolling=%2; clearing traffic IQ history.")
                                        .arg(static_cast<qulonglong>(newWin.samples.size()))
                                        .arg(static_cast<qulonglong>(rolling.samples.size())),
                                    1000);
                                rolling.clear();
                                p25Phase2ClearSpeakerPendingQueue(rx,
                                    pendingAudioByRx[sessionKey],
                                    P25PendingClearReason::RetuneOrGeneration);
                                lastPhase2DecodeByRx.erase(sessionKey);
                                {
                                    std::unique_lock<std::mutex> resetLock(rx.stateMutex, std::try_to_lock);
                                    if (resetLock.owns_lock()) {
                                        (void)tryResetP25TrafficSessionNonBlocking(rx, "iq-cursor-discontinuity", true);
                                    }
                                }
                                didWork = true;
                                continue;
                            }
                            const bool appended = rolling.append(newWin, rollingWindow);
                            const auto now = std::chrono::steady_clock::now();
                            auto& last = lastPhase2DecodeByRx[p25ReceiverSessionKey(rx)];
                            if (rolling.samples.empty()) {
                                logP25VoiceScheduler(appended ? QStringLiteral("rolling-empty") : QStringLiteral("no-new-iq"),
                                    QString("new=%1 rolling=%2 sr=%3MHz cf=%4MHz target=%5MHz tg=%6.")
                                        .arg(static_cast<qulonglong>(newWin.samples.size()))
                                        .arg(static_cast<qulonglong>(rolling.samples.size()))
                                        .arg(sr / 1e6, 0, 'f', 3)
                                        .arg(cf / 1e6, 0, 'f', 5)
                                        .arg(demodFreq / 1e6, 0, 'f', 5)
                                        .arg(monP25VoiceTalkgroupId),
                                    1500);
                                didWork = true;
                                continue;
                            }
                            // After a one-RTL MHz hop, retuneValidFromAbsolute clamps the cursor
                            // to the live edge so pre-roll of pre-retune IQ is impossible.  Capture
                            // 20260808_115158 then followed clear TG 30003 but the first live worker
                            // only saw 120 ms and never found a Phase-2 burst; replay of the same RF
                            // succeeded once it had the full two-superframe traffic eye.
                            //
                            // RollingIqWindow::append() sets decodeAbsoluteKnown=true on the first
                            // fill with lastDecodeAbsolute=startAbsolute.  Requiring
                            // !decodeAbsoluteKnown therefore never armed the wait after the first
                            // append — capture 20260712_035432 shows every post-retune WORKER START
                            // at iq=32768 and zero waiting-post-retune-iq scheduler lines.
                            {
                                const bool coldAcquireNeverDecoded =
                                    !rolling.decodeAbsoluteKnown ||
                                    rolling.lastDecodeAbsolute <= rolling.startAbsolute;
                                const bool coldAcquireEye =
                                    monP25VoicePhase2 &&
                                    rx.p25IndependentTrafficSource &&
                                    coldAcquireNeverDecoded &&
                                    !phase2SessionHadBurstEye;
                                const size_t minColdAcquireIq = (sr > 0.0)
                                    ? std::min(rollingWindow, static_cast<size_t>(
                                          std::max(32768.0, sr * kP25Phase2VoiceDecodeFirstColdEyeSeconds)))
                                    : std::min<size_t>(rollingWindow, 1474560u);
                                if (coldAcquireEye && rolling.samples.size() < minColdAcquireIq) {
                                    logP25VoiceScheduler("waiting-post-retune-iq",
                                        QString("rolling=%1 need=%2 sr=%3MHz tg=%4 target=%5MHz.")
                                            .arg(static_cast<qulonglong>(rolling.samples.size()))
                                            .arg(static_cast<qulonglong>(minColdAcquireIq))
                                            .arg(sr / 1e6, 0, 'f', 3)
                                            .arg(monP25VoiceTalkgroupId)
                                            .arg(demodFreq / 1e6, 0, 'f', 5),
                                        500);
                                    didWork = true;
                                    continue;
                                }
                            }
                            if (!appended) {
                                logP25VoiceScheduler(QStringLiteral("no-new-iq"),
                                    QString("new=%1 rolling=%2 sr=%3MHz cf=%4MHz target=%5MHz tg=%6.")
                                        .arg(static_cast<qulonglong>(newWin.samples.size()))
                                        .arg(static_cast<qulonglong>(rolling.samples.size()))
                                        .arg(sr / 1e6, 0, 'f', 3)
                                        .arg(cf / 1e6, 0, 'f', 5)
                                        .arg(demodFreq / 1e6, 0, 'f', 5)
                                        .arg(monP25VoiceTalkgroupId),
                                    3000);
                            }
                            if (last.time_since_epoch().count() != 0 &&
                                now - last < std::chrono::milliseconds(p25Phase2AdaptiveVoiceDecodeCadenceMs(rx))) {
                                didWork = true;
                                continue;
                            }
                            phase2VoiceQueueSustainHint =
                                phase2SessionSpeakerSustain ||
                                phase2EstablishedClearStreaming ||
                                p25Phase2SpeakerSustainDecodeActive() ||
                                phase2SessionHadBurstEye;
                            if (!p25VoiceWorkerCanAcceptJobForDepth(phase2VoiceQueueSustainHint)) {
                                const auto workerState = p25VoiceWorkerQueueSnapshot();
                                logP25VoiceScheduler("worker-busy",
                                    QString("pending=%1 pendingJobs=%2 busy=%3 stopping=%4 thread=%5 qDrop=%6 rDrop=%7 publish=%8 completed=%9 rolling=%10 tg=%11 target=%12MHz.")
                                        .arg(workerState.pending ? "yes" : "no")
                                        .arg(static_cast<qulonglong>(workerState.pendingJobs))
                                        .arg(workerState.busy ? "yes" : "no")
                                        .arg(workerState.stopping ? "yes" : "no")
                                        .arg(workerState.threadRunning ? "yes" : "no")
                                        .arg(workerState.droppedJobs)
                                        .arg(workerState.droppedResults)
                                        .arg(static_cast<qulonglong>(workerState.pendingPublishResults))
                                        .arg(static_cast<qulonglong>(workerState.completedResults))
                                        .arg(static_cast<qulonglong>(rolling.samples.size()))
                                        .arg(monP25VoiceTalkgroupId)
                                        .arg(demodFreq / 1e6, 0, 'f', 5),
                                    1000);
                                didWork = true;
                                continue;
                            }
                            const bool wideReacquireWindow = phase2WideReacquireWindow;
                            const bool maskEpochRepairWindow = phase2MaskEpochRepairWindow;
                            const size_t undecodedBacklog = p25Phase2UndecodedBacklogSamples(rolling);
                            // Catch up as soon as lag exceeds one sustain hop so we
                            // never leave hundreds of ms of voice RF unprocessed
                            // (20260807_232020: 60 ms decode / 350 ms wall → chop).
                            const bool activeSpeakerClearPath = phase2VoiceQueueSustainHint;
                            // Streaming DDC path: contiguous fresh-only hops (no
                            // overlap re-feed). Once sticky CQPSK is live, short
                            // catch-up thresholds keep the worker on the RF edge.
                            const bool phase2StreamingDdc =
                                monP25VoicePhase2 &&
                                rx.p25VoiceLiveDecoder.config().enableStreamingChannelDdc;
                            const double backlogCatchUpSeconds = activeSpeakerClearPath
                                ? (phase2StreamingDdc ? 0.040 : 0.050)
                                : 0.120;
                            const size_t backlogCatchUpThreshold = (sr > 0.0)
                                ? static_cast<size_t>(std::clamp(sr * backlogCatchUpSeconds, 32768.0, 1048576.0))
                                : (activeSpeakerClearPath ? 102400u : 245760u);
                            const bool preAcquiredPhase2Traffic =
                                !wideReacquireWindow &&
                                !phase2SessionHadBurstEye &&
                                !phase2SessionSpeakerSustain &&
                                !phase2EstablishedClearStreaming &&
                                monP25VoiceDecode &&
                                monP25VoicePhase2 &&
                                monP25IndependentTrafficSource &&
                                (!phase2HardTargetAcquire || maskEpochRepairWindow);
                            const bool backlogCatchUp =
                                !preAcquiredPhase2Traffic &&
                                undecodedBacklog > backlogCatchUpThreshold;
                            const bool speakerSustainEligible =
                                activeSpeakerClearPath &&
                                ((phase2StableSuperframeLock && phase2SessionHadVoiceLock) ||
                                 phase2SessionSpeakerSustain ||
                                 phase2EstablishedClearStreaming);
                            const bool speakerSustainDecode =
                                !wideReacquireWindow &&
                                !maskEpochRepairWindow &&
                                speakerSustainEligible &&
                                (phase2SustainDecodeWindow ||
                                 phase2SessionSpeakerSustain ||
                                 phase2EstablishedClearStreaming ||
                                 p25Phase2SpeakerSustainDecodeActive());
                            // Never stay on cold/unacquired after the first eye/emit —
                            // that path used minFresh=120ms and blocky 80ms islands.
                            const bool unacquiredAcquireWindow =
                                preAcquiredPhase2Traffic &&
                                !speakerSustainDecode;
                            const bool decodeCursorAdvancedPastStart =
                                rolling.submittedDecodeEndKnown ||
                                (rolling.decodeAbsoluteKnown && rolling.absoluteKnown &&
                                 rolling.effectiveDecodeAbsolute() > rolling.startAbsolute);
                            const bool firstColdEyeChunk =
                                !phase2SustainDecodeWindow &&
                                !wideReacquireWindow &&
                                !maskEpochRepairWindow &&
                                !speakerSustainDecode &&
                                !phase2HardTargetAcquire &&
                                !phase2SessionHadBurstEye &&
                                !decodeCursorAdvancedPastStart &&
                                (!rolling.decodeAbsoluteKnown ||
                                 rolling.lastDecodeAbsolute <= rolling.startAbsolute);
                            const P25Phase2VoiceChunkPlan chunkPlan = p25Phase2PlanVoiceDecodeChunk(
                                phase2StreamingDdc,
                                backlogCatchUp,
                                activeSpeakerClearPath,
                                wideReacquireWindow,
                                maskEpochRepairWindow,
                                speakerSustainDecode,
                                phase2SustainDecodeWindow,
                                firstColdEyeChunk,
                                unacquiredAcquireWindow,
                                decodeCursorAdvancedPastStart);
                            const double maxDecodeChunkSeconds = chunkPlan.maxChunkSeconds;
                            const double decodeOverlapSeconds = chunkPlan.overlapSeconds;
                            const double minDecodeFreshSeconds = chunkPlan.minFreshSeconds;
                            const double minDecodeFreshFloor = chunkPlan.minFreshFloorSamples;
                            const size_t maxDecodeChunk = (sr > 0.0)
                                ? static_cast<size_t>(std::clamp(sr * maxDecodeChunkSeconds, 32768.0, static_cast<double>(rollingWindow)))
                                : std::min<size_t>(rolling.samples.size(), rollingWindow);
                            const size_t maxOverlapSamples = (rollingWindow > maxDecodeChunk)
                                ? (rollingWindow - maxDecodeChunk)
                                : 0;
                            const size_t decodeOverlap = (sr > 0.0)
                                ? static_cast<size_t>(std::clamp(sr * decodeOverlapSeconds, 0.0, static_cast<double>(maxOverlapSamples)))
                                : std::min<size_t>(maxOverlapSamples, rolling.samples.size());
                            const size_t minDecodeFreshNominal = (sr > 0.0)
                                ? static_cast<size_t>(std::clamp(sr * minDecodeFreshSeconds, 1.0, static_cast<double>(maxDecodeChunk)))
                                : 4096u;
                            const size_t minDecodeFresh = p25Phase2EffectiveMinFreshSamples(
                                rolling.samples.size(), decodeOverlap, minDecodeFreshNominal,
                                static_cast<size_t>(minDecodeFreshFloor), maxDecodeChunk);
                            // Phase 2 TDMA must reach the decoder in complete
                            // AMBE cadence units.  Letting live-edge pressure
                            // soften a 40 ms speaker slice down to 16-32 ms
                            // burns worker passes and leaves the output ring
                            // playing isolated words.
                            iq = rolling.takeUndecoded(maxDecodeChunk, decodeOverlap, iqStartAbsolute, iqStartAbsoluteKnown,
                                &phase2FreshIqSamples, &phase2ContextIqSamples, minDecodeFresh,
                                &iqDecodeEndAbsolute, &iqDecodeEndAbsoluteKnown, false);
                            if (iq.empty()) {
                                logP25VoiceScheduler("waiting-fresh-iq",
                                    QString("rolling=%1 maxChunk=%2 overlap=%3 minFresh=%4 effMinFresh=%5 absKnown=%6 tg=%7 target=%8MHz.")
                                        .arg(static_cast<qulonglong>(rolling.samples.size()))
                                        .arg(static_cast<qulonglong>(maxDecodeChunk))
                                        .arg(static_cast<qulonglong>(decodeOverlap))
                                        .arg(static_cast<qulonglong>(minDecodeFreshNominal))
                                        .arg(static_cast<qulonglong>(minDecodeFresh))
                                        .arg(iqStartAbsoluteKnown ? "yes" : "no")
                                        .arg(monP25VoiceTalkgroupId)
                                        .arg(demodFreq / 1e6, 0, 'f', 5),
                                    1500);
                                didWork = true;
                                continue;
                            }
                            if (chunkPlan.treatAsContextFreeFresh) {
                                phase2FreshIqSamples = iq.size();
                                phase2ContextIqSamples = 0;
                            }
                            last = now;
                        } else {
                            if (!monP25VoiceDecode && (monMode == DemodMode::WFM || monMode == DemodMode::NFM)) {
                                // Emergency-only catch-up. A tight 120 ms threshold was thrashing
                                // discontinuities every few blocks → helicopter chop. Allow ~400 ms
                                // of IQ slack so the demod FIR/squelch stay continuous.
                                const size_t analogMaxLag = (sr > 0.0)
                                    ? static_cast<size_t>(std::clamp(sr * 0.400, 32768.0, sr * 0.600))
                                    : 32768;
                                auto window = mgr.getNewIQWindowForReceiver(i, rx, tgt, analogMaxLag);
                                rdsStreamEpoch = window.streamEpoch;
                                rdsIqStart = window.startAbsolute;
                                rdsSourceGap = window.cursorDiscontinuity;
                                iq = std::move(window.samples);
                            } else {
                                iq = mgr.getNewSamplesForReceiver(i, rx, tgt);
                            }
                        }
                    }

                    const size_t got = iq.size();
                    if (got == 0) {
                        if (monP25VoiceDecode && monP25VoicePhase2) {
                            logP25VoiceScheduler("empty-iq-after-pull",
                                QString("sr=%1MHz cf=%2MHz target=%3MHz tg=%4; receiver cursor returned no IQ.")
                                    .arg(sr / 1e6, 0, 'f', 3)
                                    .arg(cf / 1e6, 0, 'f', 5)
                                    .arg(demodFreq / 1e6, 0, 'f', 5)
                                    .arg(monP25VoiceTalkgroupId),
                                1500);
                        }
                        continue;
                    }

                    const double t = got / sr;
                    const double orate = (engineForAudio ? engineForAudio->getSampleRate() : 48000.0);
                    const size_t need = (size_t)std::round(t * orate);
                    if (monP25VoiceDecode && monP25VoicePhase2) {
                        MainWindow::P25VoiceDecodeJob job;
                        job.rx = rxPtr;
                        job.iq = std::move(iq);
                        job.audioOutputIndices = rxAudioOutputs;
                        job.sampleRateHz = sr;
                        job.centerFreqHz = cf;
                        job.targetFreqHz = demodFreq;
                        job.outputRateHz = orate;
                        job.iqStartAbsolute = iqStartAbsolute;
                        job.iqStartAbsoluteKnown = iqStartAbsoluteKnown;
                        job.iqDecodeEndAbsolute = iqDecodeEndAbsolute;
                        job.iqDecodeEndAbsoluteKnown = iqDecodeEndAbsoluteKnown;
                        job.outputMutedForSettle = p25VoiceOutputMutedForSettle;
                        job.rollingDecode = phase2BufferedDecode;
                        job.speakerSustainDecode = phase2VoiceQueueSustainHint;
                        job.freshIqSamples = phase2FreshIqSamples;
                        job.contextIqSamples = phase2ContextIqSamples;
                        job.trafficGeneration = monP25TrafficGeneration;
                        job.independentTrafficSource = monP25IndependentTrafficSource;
                        job.talkgroupId = monP25VoiceTalkgroupId;
                        job.sourceId = monP25VoiceSourceId;
                        job.tdmaSlotKnown = monP25VoiceTdmaSlotKnown;
                        job.tdmaSlot = monP25VoiceTdmaSlot;
                        job.voiceFreqHz = monP25TrafficVoiceFreqHz;
                        job.flushSeq = p25PendingAudioFlushSeq.load(std::memory_order_acquire);
                        if (rxPtr) {
                            job.receiverSessionKey = p25ReceiverSessionKey(*rxPtr);
                            job.callSessionId = rxPtr->p25CurrentCallSessionId;
                        }
                        if (!submitP25VoiceDecodeJob(std::move(job))) {
                            const auto workerState = p25VoiceWorkerQueueSnapshot();
                            const QString submitDetail = QString("pending=%1 pendingJobs=%2 busy=%3 stopping=%4 thread=%5 nextSeq=%6 qDrop=%7 rDrop=%8 publish=%9 completed=%10.")
                                .arg(workerState.pending ? "yes" : "no")
                                .arg(static_cast<qulonglong>(workerState.pendingJobs))
                                .arg(workerState.busy ? "yes" : "no")
                                .arg(workerState.stopping ? "yes" : "no")
                                .arg(workerState.threadRunning ? "yes" : "no")
                                .arg(static_cast<qulonglong>(workerState.nextSequence))
                                .arg(workerState.droppedJobs)
                                .arg(workerState.droppedResults)
                                .arg(static_cast<qulonglong>(workerState.pendingPublishResults))
                                .arg(static_cast<qulonglong>(workerState.completedResults));
                            logP25VoiceScheduler("submit-failed", submitDetail, 1000);
                            QTimer::singleShot(0, this, [this]() {
                                appendP25LogLineKeyed("p25-voice-worker-submit-failed",
                                    "P25 voice worker submit failed; Phase 2 IQ window was dropped because the worker is stopping or unavailable.",
                                    2500);
                            });
                        } else {
                            if (phase2BufferedDecode && iqDecodeEndAbsoluteKnown) {
                                phase2IqByRx[p25ReceiverSessionKey(rx)].markDecodeSubmitted(iqDecodeEndAbsolute);
                            }
                            logP25VoiceScheduler("submitted",
                                QString("iq=%1 fresh=%2 context=%3 absKnown=%4 sr=%5MHz cf=%6MHz target=%7MHz tg=%8 slot=%9 generation=%10.")
                                    .arg(static_cast<qulonglong>(got))
                                    .arg(static_cast<qulonglong>(phase2FreshIqSamples))
                                    .arg(static_cast<qulonglong>(phase2ContextIqSamples))
                                    .arg(iqStartAbsoluteKnown ? "yes" : "no")
                                    .arg(sr / 1e6, 0, 'f', 3)
                                    .arg(cf / 1e6, 0, 'f', 5)
                                    .arg(demodFreq / 1e6, 0, 'f', 5)
                                    .arg(monP25VoiceTalkgroupId)
                                    .arg(monP25VoiceTdmaSlotKnown ? QString::number(monP25VoiceTdmaSlot & 0x01u) : QStringLiteral("unknown"))
                                    .arg(static_cast<qulonglong>(monP25TrafficGeneration)),
                                    150);
                        }
                        gLastAfcOffsetHz.store(afcOffsetHz);
                        if (rfMetrics.valid) {
                            gLastRmsDb.store(rfMetrics.signalLevelDb);
                            gLastNoiseFloorDb.store(rfMetrics.noiseFloorDb);
                            gLastSnrDb.store(rfMetrics.snrDb);
                        }
                        didWork = true;
                        continue;
                    }
                    auto t0 = std::chrono::steady_clock::now();
                    bool haveP25Audio = false;
                    bool publishVoiceDiag = false;
                    bool effectiveSpeakerMayEmit = false;
                    P25VoiceAudioBlock p25Audio;
                    {
                        std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                        if (!dspLock.owns_lock()) {
                            didWork = true;
                            continue;
                        }
                        if (monMode != DemodMode::NFM || monP25ControlMute || monP25VoiceDecode || rdsSourceGap)
                            rx.sstvFeed->discontinuity(); // Metadata only; never touches the P25/audio decoder.
                        if (p25ShouldSuppressAnalogDemod(monP25VoiceDecode,
                                                         monP25ControlMute,
                                                         monP25IndependentTrafficSource,
                                                         monP25VoicePhase2) &&
                            !monP25VoiceDecode) {
                            rms = -120.0;
                            (void)need;
                        } else if (monP25VoiceDecode) {
                            p25Audio = decodeP25VoiceAudioBlock(rx, iq, sr, cf, demodFreq, orate,
                                iqStartAbsolute, iqStartAbsoluteKnown, phase2ContextIqSamples);
                            (void)phase2FreshIqSamples;
                            // Do not sample-trim Phase-2 PCM here.  Audio de-duplication is now
                            // done at AMBE codeword granularity using absolute dibit positions in
                            // decodeP25Phase2VoiceBlock().  The previous fresh-tail sample limit
                            // could collapse a valid multi-frame superframe decode to ~100 ms,
                            // producing the fast/jittery doubled-frame sound reported in field logs.
                            bool dropStaleTrafficAudio = false;
                            if (rx.p25IndependentTrafficSource) {
                                const uint64_t liveGen = p25TrafficSourceGeneration.load(std::memory_order_acquire);
                                std::unique_lock<std::mutex> genLock(rx.stateMutex, std::try_to_lock);
                                if (!genLock.owns_lock() || !rx.active ||
                                    rx.p25TrafficGeneration == 0 || rx.p25TrafficGeneration != liveGen) {
                                    dropStaleTrafficAudio = true;
                                }
                            }
                            if (dropStaleTrafficAudio) {
                                didWork = true;
                                continue;
                            }
                            publishVoiceDiag = true;
                            haveP25Audio = true;
                            const std::string rawSpeakerGateReason = p25VoiceBlockSpeakerGateReason(p25Audio);
                            const bool settleMuteBypassedForValidatedVoice =
                                p25VoiceOutputMutedForSettle &&
                                p25VoiceBlockMayBypassPostArmSettle(p25Audio, rawSpeakerGateReason);
                            const bool effectiveSettleMute =
                                p25VoiceOutputMutedForSettle && !settleMuteBypassedForValidatedVoice;
                            const std::string speakerGateReason = effectiveSettleMute
                                ? std::string("post-arm-settle-muted")
                                : rawSpeakerGateReason;
                            const bool speakerMayEmit =
                                speakerGateReason == "emit" &&
                                p25VoiceBlockHasSpeakerTimelineAudio(p25Audio);
                            p25Audio.phase2SpeakerGateReason = speakerGateReason;
                            const double curSnrForAudio = gLastSnrDb.load(std::memory_order_relaxed);
                            const bool hasCarrierForAudio = (curSnrForAudio > 2.0) ||
                                (gLastRmsDb.load(std::memory_order_relaxed) > gLastNoiseFloorDb.load(std::memory_order_relaxed) + 4.0);
                            const bool effectiveSpeakerMayEmitLocal = speakerMayEmit &&
                                (hasCarrierForAudio ||
                                 (p25Audio.phase2EmittedPcmFrames > 0 &&
                                  p25VoiceBlockHasSpeakerTimelineAudio(p25Audio)) ||
                                 (p25Audio.phase2FedToMbelib > 0 && p25Audio.phase2TargetVoiceCodewords > 0));
                            effectiveSpeakerMayEmit = effectiveSpeakerMayEmitLocal;
                            ch = effectiveSpeakerMayEmitLocal ? p25Audio.audio : std::vector<float>{};
                            audioOutputEngine = ch.empty()
                                ? engineForAudio.get()
                                : ensureAudioOutputActive("P25 voice");
                            if (audioOutputEngine) {
                                audioActiveOutputCount = audioOutputEngine->activeOutputCount();
                                audioQueuedSamples = audioOutputEngine->getRingQueuedSamples();
                                audioRingFillPercent = audioOutputEngine->getRingFillPercent();
                                audioUnderrunCount = audioOutputEngine->getUnderrunCount();
                            }
                            writeP25Phase2AudioOutputTrace(rx, p25Audio, "gui-dsp-worker",
                                p25VoiceOutputMutedForSettle, speakerMayEmit, audioOutputEngine != nullptr,
                                audioActiveOutputCount, audioQueuedSamples, audioRingFillPercent,
                                audioUnderrunCount, ch.size(), orate);
                            if (!ch.empty()) {
                                double sum = 0.0;
                                for (float sample : ch) sum += static_cast<double>(sample) * sample;
                                rms = 20.0 * std::log10(std::sqrt(sum / static_cast<double>(ch.size())) + 1e-12);
                            }
                            (void)need;
                        } else {
                            FmMultiplexBlock mpx;
                            const bool decodeRds = monMode == DemodMode::WFM && !monP25ControlMute;
                            const bool decodeTones = monMode == DemodMode::NFM && !monP25ControlMute;
                            const bool decodeData = decodeRds || decodeTones;

                            bool repEnabled = false, repDualWanted = false, repLogDtmf = false,
                                repLogTones = false, repLogCarrier = false;
                            double repOutHz = 0.0, repInHz = 0.0;
                            {
                                std::lock_guard<std::mutex> lock(repeaterMonitorMutex);
                                repEnabled = repeaterMonitorEnabled;
                                repDualWanted = repeaterDualWatchWanted;
                                repLogDtmf = repeaterLogDtmf;
                                repLogTones = repeaterLogTones;
                                repLogCarrier = repeaterLogCarrier;
                                repOutHz = repeaterOutputHz;
                                repInHz = repeaterInputHz;
                            }

                            double audioTargetHz = demodFreq;
                            double primaryIdentityHz = monFreq;
                            bool dualActive = false;
                            QString dualReason = QStringLiteral("disabled");
                            if (repEnabled && repDualWanted && decodeTones) {
                                const auto plan = planRepeaterDualWatch(repOutHz, repInHz, sr, monBw);
                                if (!plan.feasible) {
                                    dualReason = QString::fromUtf8(plan.reason);
                                    rx.repeaterDualWatchCentered = false;
                                } else {
                                    const bool inBandNow =
                                        frequencyInPassband(repOutHz, cf, sr, monBw) &&
                                        frequencyInPassband(repInHz, cf, sr, monBw);
                                    if (!inBandNow && !rx.repeaterDualWatchCentered &&
                                        std::abs(cf - plan.centerHz) > std::max(25e3, monBw * 2.0)) {
                                        // One-shot soft center only — never spam setCenterFreq.
                                        mgr.setCenterFreq(i, plan.centerHz);
                                        cf = plan.centerHz;
                                        rx.repeaterDualWatchCentered = true;
                                        dualReason = QStringLiteral("centering for pair");
                                    }
                                    if (frequencyInPassband(repOutHz, cf, sr, monBw) &&
                                        frequencyInPassband(repInHz, cf, sr, monBw)) {
                                        dualActive = true;
                                        dualReason = QStringLiteral("active in passband");
                                        audioTargetHz = repOutHz;
                                        primaryIdentityHz = repOutHz;
                                        rx.repeaterDualWatchCentered = true;
                                    } else if (rx.repeaterDualWatchCentered) {
                                        dualReason = QStringLiteral("waiting for retune settle");
                                    } else {
                                        dualReason = QStringLiteral("pair outside current passband");
                                    }
                                }
                            } else if (repEnabled && !repDualWanted) {
                                dualReason = QStringLiteral("dual-watch unchecked");
                                rx.repeaterDualWatchCentered = false;
                            } else if (repEnabled) {
                                dualReason = QStringLiteral("NFM required");
                                rx.repeaterDualWatchCentered = false;
                            } else {
                                rx.repeaterDualWatchCentered = false;
                            }
                            {
                                std::lock_guard<std::mutex> lock(repeaterMonitorMutex);
                                repeaterDualWatchActive = dualActive;
                                repeaterDualWatchReason = dualReason;
                            }

                            if (decodeData && (rdsSourceGap || rx.rdsIqEpoch != rdsStreamEpoch || rx.rdsNextIq != rdsIqStart)) {
                                rx.demod.resetMultiplexState();
                                rx.rds->reset();
                                rx.ctcss.reset();
                                rx.dcs.reset();
                                if (repEnabled) rx.dtmf.reset();
                            }
                            ch = rx.demod.demodulateToAudio(iq, sr, cf, audioTargetHz, monMode,
                                rms, monLpf, monSquelch, monGain, monWfmDe,
                                monWfmNotch, monBw, need, orate, rfSquelchLevel, monAudioLpfEnabled,
                                decodeData && !rdsSourceGap ? &mpx : nullptr,
                                decodeTones ? primaryIdentityHz : std::numeric_limits<double>::quiet_NaN());
                            if (decodeData && !rdsSourceGap) {
                                if (decodeRds) {
                                    rx.ctcss.reset();
                                    rx.dcs.reset();
                                    if (repEnabled) rx.dtmf.reset();
                                    rx.rds->process({mpx.samples, DecoderInputDomain::FmMultiplex,
                                        mpx.sampleRate, mpx.targetHz, i, mpx.epoch, mpx.firstSample, mpx.discontinuity});
                                } else {
                                    rx.rds->reset();
                                    if (mpx.discontinuity && mpx.epoch <= 32) spdlog::info("NFM data reset reasons={} rate={} bw={} epoch={} iqEpoch={} iqStart={} previousEnd={}",
                                        mpx.resetReasons, mpx.sampleRate, monBw, mpx.epoch, rdsStreamEpoch, rdsIqStart, rx.rdsNextIq);
                                    rx.ctcss.process(mpx.samples, mpx.sampleRate, primaryIdentityHz,
                                        mpx.epoch, mpx.firstSample, mpx.discontinuity);
                                    rx.dcs.process(mpx.samples, mpx.sampleRate, primaryIdentityHz,
                                        mpx.epoch, mpx.firstSample, mpx.discontinuity);
                                    if (repEnabled) {
                                        rx.dtmf.process(mpx.samples, mpx.sampleRate, primaryIdentityHz,
                                            mpx.epoch, mpx.firstSample, mpx.discontinuity);
                                        const auto channel = dualActive ? ControlEvent::Channel::Output
                                                                        : ControlEvent::Channel::Tuned;
                                        if (repLogDtmf)
                                            drainDtmfEvents(rx.dtmf, rx.controlEvents, channel, primaryIdentityHz);
                                        noteToneChanges(rx.controlEvents, channel, primaryIdentityHz,
                                            rx.ctcss.snapshot(), rx.lastLoggedCtcssHz,
                                            rx.dcs.snapshot(), rx.lastLoggedDcsKey, repLogTones);
                                        const bool carrierOpen = std::isfinite(rms) && rms > (monSquelch + 1.0);
                                        noteCarrier(rx.controlEvents, channel, primaryIdentityHz,
                                            carrierOpen, rx.controlCarrierOpen, repLogCarrier,
                                            RdsMpxDecoder::monotonicMs());
                                    }
                                    rx.sstvFeed->publish(mpx, i);
                                }
                                rx.rdsIqEpoch = rdsStreamEpoch;
                                rx.rdsNextIq = rdsIqStart + iq.size();
                            } else {
                                rx.rds->reset();
                                rx.ctcss.reset();
                                rx.dcs.reset();
                                if (repEnabled) rx.dtmf.reset();
                                rx.rdsIqEpoch = rx.rdsNextIq = 0;
                            }

                            // Silent input-leg DDC when dual-watch is active (no speaker audio).
                            // Rate-limit to every 4th block so NFM audio stays realtime; DTMF still
                            // catches keypad bursts which last tens of ms.
                            if (repEnabled && dualActive && decodeTones && !rdsSourceGap) {
                                ++rx.inputWatchSkipCounter;
                                const bool runInputWatch = (rx.inputWatchSkipCounter % 4u) == 1u;
                                if (runInputWatch) {
                                    FmMultiplexBlock inMpx;
                                    const bool inGap = rx.inputWatchIqEpoch != rdsStreamEpoch ||
                                        rx.inputWatchNextIq != rdsIqStart;
                                    if (inGap) {
                                        rx.inputWatchDemod.resetMultiplexState();
                                        rx.inputWatchCtcss.reset();
                                        rx.inputWatchDcs.reset();
                                        rx.inputWatchDtmf.reset();
                                    }
                                    double inRms = -200.0;
                                    (void)rx.inputWatchDemod.demodulateToAudio(iq, sr, cf, repInHz, DemodMode::NFM,
                                        inRms, monLpf, monSquelch, 1.0, monWfmDe, monWfmNotch, monBw,
                                        0, orate, rfSquelchLevel, false, &inMpx, repInHz);
                                    rx.inputWatchCtcss.process(inMpx.samples, inMpx.sampleRate, repInHz,
                                        inMpx.epoch, inMpx.firstSample, inMpx.discontinuity || inGap);
                                    rx.inputWatchDcs.process(inMpx.samples, inMpx.sampleRate, repInHz,
                                        inMpx.epoch, inMpx.firstSample, inMpx.discontinuity || inGap);
                                    rx.inputWatchDtmf.process(inMpx.samples, inMpx.sampleRate, repInHz,
                                        inMpx.epoch, inMpx.firstSample, inMpx.discontinuity || inGap);
                                    if (repLogDtmf)
                                        drainDtmfEvents(rx.inputWatchDtmf, rx.controlEvents,
                                            ControlEvent::Channel::Input, repInHz);
                                    noteToneChanges(rx.controlEvents, ControlEvent::Channel::Input, repInHz,
                                        rx.inputWatchCtcss.snapshot(), rx.inputLastLoggedCtcssHz,
                                        rx.inputWatchDcs.snapshot(), rx.inputLastLoggedDcsKey, repLogTones);
                                    const bool inCarrier = std::isfinite(inRms) && inRms > (monSquelch + 1.0);
                                    noteCarrier(rx.controlEvents, ControlEvent::Channel::Input, repInHz,
                                        inCarrier, rx.inputWatchCarrierOpen, repLogCarrier,
                                        RdsMpxDecoder::monotonicMs());
                                    rx.inputWatchIqEpoch = rdsStreamEpoch;
                                    rx.inputWatchNextIq = rdsIqStart + iq.size();
                                }
                            } else if (!dualActive) {
                                rx.inputWatchIqEpoch = rx.inputWatchNextIq = 0;
                                rx.inputWatchSkipCounter = 0;
                            }

                            if (!repEnabled) {
                                if (rx.repeaterMonitorWasEnabled) {
                                    rx.dtmf.reset();
                                    rx.inputWatchDtmf.reset();
                                    rx.inputWatchCtcss.reset();
                                    rx.inputWatchDcs.reset();
                                    rx.inputWatchDemod.resetMultiplexState();
                                    rx.inputWatchIqEpoch = rx.inputWatchNextIq = 0;
                                    rx.controlCarrierOpen = false;
                                    rx.inputWatchCarrierOpen = false;
                                    rx.repeaterMonitorWasEnabled = false;
                                    rx.repeaterDualWatchCentered = false;
                                    rx.inputWatchSkipCounter = 0;
                                }
                            } else {
                                rx.repeaterMonitorWasEnabled = true;
                            }
                        }
                    }
                    if (haveP25Audio) {
                        publishP25VoiceDiagnostics(rx, p25Audio, publishVoiceDiag);
                        if (monP25VoiceDecode && monP25VoicePhase2) {
                            const uintptr_t rxKey = reinterpret_cast<uintptr_t>(&rx);
                            const long long absStartLog = iqStartAbsoluteKnown ? static_cast<long long>(iqStartAbsolute) : -1LL;
                            const size_t gotLog = got;
                            const bool rollingLog = phase2BufferedDecode;
                            const double cfLog = cf;
                            const double targetLog = p25Audio.effectiveTargetFreqHz > 0.0
                                ? p25Audio.effectiveTargetFreqHz
                                : demodFreq;
                            const double srLog = sr;
                            const QString diagLog = p25VoiceDiagLabel(p25Audio.diag);
                            const QString backendLog = p25Audio.backendAvailable ? QStringLiteral("yes") : QStringLiteral("no");
                            const QString essLog = p25Audio.phase2EssKnown
                                ? (p25Audio.phase2EssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
                                : QStringLiteral("unknown");
                            const QString acchLog = QString("p2acch=nom:%1 altKind:%2 swap:%3 slip:%4 inv:%5")
                                .arg(static_cast<qulonglong>(p25Audio.phase2MacNominalCrcValid))
                                .arg(static_cast<qulonglong>(p25Audio.phase2MacAltKindCrcValid))
                                .arg(static_cast<qulonglong>(p25Audio.phase2MacBitSwapCrcValid))
                                .arg(static_cast<qulonglong>(p25Audio.phase2MacSlipCrcValid))
                                .arg(static_cast<qulonglong>(p25Audio.phase2MacInvertCrcValid));
                            const qulonglong syncsLog = static_cast<qulonglong>(p25Audio.syncs);
                            const qulonglong nidsLog = static_cast<qulonglong>(p25Audio.nids);
                            const qulonglong imbeLog = static_cast<qulonglong>(p25Audio.imbeFrames);
                            const qulonglong decodedLog = static_cast<qulonglong>(p25Audio.decodedFrames);
                            const qulonglong audioSamplesLog = static_cast<qulonglong>(p25Audio.audio.size());
                            const qulonglong burstsLog = static_cast<qulonglong>(p25Audio.phase2Bursts);
                            const qulonglong vcwLog = static_cast<qulonglong>(p25Audio.phase2VoiceCodewords);
                            const qulonglong targetVcwLog = static_cast<qulonglong>(p25Audio.phase2TargetVoiceCodewords);
                            const qulonglong oppVcwLog = static_cast<qulonglong>(p25Audio.phase2OppositeVoiceCodewords);
                            const qulonglong ambeAttemptsLog = static_cast<qulonglong>(p25Audio.phase2AmbeDecodeAttempts);
                            const qulonglong ambeAcceptedLog = static_cast<qulonglong>(p25Audio.phase2AmbeAcceptedFrames);
                            const qulonglong rejectedVcwLog = static_cast<qulonglong>(p25Audio.phase2RejectedVoiceCodewords);
                            const qulonglong wrongSlotVcwLog = static_cast<qulonglong>(p25Audio.phase2WrongSlotVoiceCodewords);
                            const qulonglong duplicateVcwLog = static_cast<qulonglong>(p25Audio.phase2DuplicateSuppressedVoiceCodewords);
                            const qulonglong absDuplicateVcwLog = static_cast<qulonglong>(p25Audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords);
                            const qulonglong seqSuppressVcwLog = static_cast<qulonglong>(p25Audio.phase2SequencerSuppressedVoiceCodewords);
                            const qulonglong sfLog = static_cast<qulonglong>(p25Audio.phase2SuperframeBursts);
                            const qulonglong maskLog = static_cast<qulonglong>(p25Audio.phase2MaskedBursts);
                            const qulonglong macValidLog = static_cast<qulonglong>(p25Audio.phase2MacCrcValid);
                            const qulonglong macTotalLog = static_cast<qulonglong>(p25Audio.phase2MacPdus);
                            const qulonglong expVcwLog = static_cast<qulonglong>(p25Audio.phase2ExpectedVoiceCodewords);
                            const qulonglong fedLog = static_cast<qulonglong>(p25Audio.phase2FedToMbelib);
                            const qulonglong emitLog = static_cast<qulonglong>(p25Audio.phase2EmittedPcmFrames);
                            const qulonglong gapsLog = static_cast<qulonglong>(p25Audio.phase2FeedGaps);
                            const qulonglong contextVcwLog = static_cast<qulonglong>(p25Audio.phase2ContextVoiceCodewords);
                            const qulonglong contextDropLog = static_cast<qulonglong>(p25Audio.phase2ContextSuppressedVoiceCodewords);
                            const QString sourceLog = rx.p25VoiceSourceId != 0
                                ? p25HexId(rx.p25VoiceSourceId, 6)
                                : QStringLiteral("unknown");
                            const qulonglong callSessionLog = static_cast<qulonglong>(rx.p25CurrentCallSessionId);
                            const qlonglong grantEpochLog = static_cast<qlonglong>(rx.p25VoiceGrantEpochMs);
                            const qulonglong pttGenerationLog = static_cast<qulonglong>(rx.p25PttGeneration);
                            QTimer::singleShot(0, this, [this, rxKey, absStartLog, gotLog, rollingLog, cfLog, targetLog, srLog,
                                                          diagLog, backendLog, syncsLog, nidsLog, imbeLog, decodedLog,
                                                          audioSamplesLog, burstsLog, vcwLog, targetVcwLog, oppVcwLog,
                                                           ambeAttemptsLog, ambeAcceptedLog,
                                                           rejectedVcwLog, wrongSlotVcwLog, duplicateVcwLog,
                                                           absDuplicateVcwLog, seqSuppressVcwLog,
                                                           sfLog, maskLog,
                                                          macValidLog, macTotalLog, acchLog, essLog,
                                                          expVcwLog, fedLog, emitLog, gapsLog,
                                                          contextVcwLog, contextDropLog,
                                                          sourceLog, callSessionLog, grantEpochLog, pttGenerationLog]() {
                                const QString key = QString("p25-dsp-voice-loop:%1").arg(static_cast<qulonglong>(rxKey));
                                const QString line = QString("P25 DSP VOICE LOOP: rolling=%1 iq=%2 absStart=%3 sr=%4MHz cf=%5MHz target=%6MHz diag=%7 backend=%8 sync=%9 nid=%10 imbe=%11 decoded=%12 audio=%13 p2bursts=%14 p2vcw=%15 targetVcw=%16 oppVcw=%17 exp=%18 fed=%19 emit=%20 gaps=%21 ctxVcw=%22 ctxDrop=%23 reject=%24 wrongSlot=%25 dup=%26 absDup=%27 seqDrop=%28 p2sf=%29 p2mask=%30 p2mac=%31/%32 %33 ess=%34 src=%35 call=%36 grantEpoch=%37 pttGen=%38")
                                    .arg(rollingLog ? "yes" : "no")
                                    .arg(static_cast<qulonglong>(gotLog))
                                    .arg(absStartLog)
                                    .arg(srLog / 1e6, 0, 'f', 3)
                                    .arg(cfLog / 1e6, 0, 'f', 5)
                                    .arg(targetLog / 1e6, 0, 'f', 5)
                                    .arg(diagLog)
                                    .arg(backendLog)
                                    .arg(syncsLog)
                                    .arg(nidsLog)
                                    .arg(imbeLog)
                                    .arg(decodedLog)
                                    .arg(audioSamplesLog)
                                    .arg(burstsLog)
                                    .arg(vcwLog)
                                    .arg(targetVcwLog)
                                    .arg(oppVcwLog)
                                    .arg(expVcwLog)
                                    .arg(fedLog)
                                    .arg(emitLog)
                                    .arg(gapsLog)
                                    .arg(contextVcwLog)
                                    .arg(contextDropLog)
                                    .arg(rejectedVcwLog)
                                    .arg(wrongSlotVcwLog)
                                    .arg(duplicateVcwLog)
                                    .arg(absDuplicateVcwLog)
                                    .arg(seqSuppressVcwLog)
                                    .arg(sfLog)
                                    .arg(maskLog)
                                    .arg(macValidLog)
                                    .arg(macTotalLog)
                                    .arg(acchLog)
                                    .arg(essLog)
                                    .arg(sourceLog)
                                    .arg(callSessionLog)
                                    .arg(grantEpochLog)
                                    .arg(pttGenerationLog);
                                appendP25LogLineKeyed(key, line, 750);
                            });
                        }
                    }
                    auto t1 = std::chrono::steady_clock::now();
                    dspMicros = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

                    gLastDspMicros.store(dspMicros);
                    gLastAfcOffsetHz.store(afcOffsetHz);
                    if (rfMetrics.valid) {
                        gLastRmsDb.store(rfMetrics.signalLevelDb);
                        gLastNoiseFloorDb.store(rfMetrics.noiseFloorDb);
                        gLastSnrDb.store(rfMetrics.snrDb);
                    } else {
                        gLastRmsDb.store(rms);
                    }
                    if (!ch.empty()) {
                        if (!audioOutputEngine) audioOutputEngine = ensureAudioOutputActive("decoded audio");
                        if (audioOutputEngine && audioOutputEngine->activeOutputCount() > 0) {
                            if (haveP25Audio && monP25VoiceDecode) {
                                // Suppress pure tail/conceal when no fresh target voice in block (prevents post-signal bursts)
                                // But for established Phase-2 clear calls, do not clear pending here: windows can legitimately
                                // contribute 0 new in a given slice while prior audio is still playing. Clearing causes blocky/not-joined.
                                size_t pushedSamples = 0;
                                const qint64 pendingNowMs = QDateTime::currentMSecsSinceEpoch();
                                const bool gateEmit =
                                    effectiveSpeakerMayEmit &&
                                    p25Audio.phase2SpeakerGateReason == "emit" &&
                                    p25VoiceBlockMayEmitAudio(p25Audio) &&
                                    !p25Audio.phase2StaleAudioTail;
                                auto& pendingSpeaker = p25SpeakerPendingFor(pendingAudioByRx, rx);
                                p25Phase2BindSpeakerPendingToCall(pendingSpeaker, rx);
                                const bool hasNewPcm =
                                    p25VoiceBlockHasSpeakerTimelineAudio(p25Audio) &&
                                    p25Audio.phase2EmittedPcmFrames > 0;
                                std::vector<float> pushedRealAudio;
                                std::vector<float> speakerAudioForQueue;
                                const std::vector<float>* speakerAudioToQueue = &p25Audio.audio;
                                if (monP25VoicePhase2 && hasNewPcm) {
                                    const double outRate = std::max(8000.0,
                                        static_cast<double>(audioOutputEngine->getSampleRate()));
                                    const size_t phase2FrameSamples = std::max<size_t>(160,
                                        static_cast<size_t>(outRate * 0.020 + 0.5));
                                    speakerAudioForQueue = p25Phase2SpeakerAudioForQueue(
                                        pendingSpeaker, p25Audio, p25Audio.audio, phase2FrameSamples);
                                    speakerAudioToQueue = &speakerAudioForQueue;
                                }
                                const bool hasPlayableNewPcm = !speakerAudioToQueue->empty();
                                if (gateEmit && (hasPlayableNewPcm || !pendingSpeaker.samples.empty())) {
                                    pushedSamples = pushP25SpeakerAudio(audioOutputEngine,
                                        pendingSpeaker.samples,
                                        *speakerAudioToQueue,
                                        rxAudioOutputs,
                                        audioOutputEngine->getRingFillPercent(),
                                        !hasPlayableNewPcm,
                                        &pushedRealAudio);
                                }
                                // Never clear accepted speaker PCM on transient gate failure.
                                // Playback queue is cleared only on explicit call boundaries.
                                if (pushedSamples > 0) {
                                    p25Phase2ResetPlayoutBridge(rx);
                                    const P25P2CallAudioKey speakerKey =
                                        p25CurrentPhase2AudioKey(rx, p25Audio.effectiveTargetFreqHz);
                                    const bool bridgeAnchor =
                                        p25Phase2CleanPlayoutBridgeAnchorWindow(p25Audio);
                                    p25Phase2RememberLastEmittedSample(
                                        rx, speakerKey, pushedRealAudio.empty() ? ch : pushedRealAudio,
                                        bridgeAnchor);
                                // Tap clear speaker PCM for Decode Log STT (async; never blocks DSP).
                                // Encrypted / gated-silent paths never reach here.
                                {
                                    const int sttRate = static_cast<int>(
                                        std::lround(std::max(8000.0,
                                            static_cast<double>(audioOutputEngine->getSampleRate()))));
                                    const double sttFreq = p25TranscriptVoiceLabelHz(
                                        rx, p25Audio.effectiveTargetFreqHz, demodFreq);
                                    const int sttSlot = rx.p25VoiceTdmaSlotKnown
                                        ? static_cast<int>(rx.p25VoiceTdmaSlot & 0x01u)
                                        : -1;
                                    const std::vector<float>& transcriptAudio =
                                        pushedRealAudio.empty() ? ch : pushedRealAudio;
                                    p25TranscriptTapSpeakerPcm(transcriptAudio.data(), transcriptAudio.size(),
                                        sttRate, p25Audio.talkgroupId, sttFreq, sttSlot,
                                        p25Audio.effectiveTargetFreqHz);
                                }
                                const size_t activeCount = audioOutputEngine->activeOutputCount();
                                const size_t queuedAfter = audioOutputEngine->getRingQueuedSamples();
                                const double fillAfter = audioOutputEngine->getRingFillPercent();
                                const int underrunsAfter = audioOutputEngine->getUnderrunCount();
                                if (haveP25Audio && monP25VoiceDecode) {
                                    guiP25AudioOutputEvents.fetch_add(1, std::memory_order_relaxed);
                                    guiP25AudioOutputSamples.fetch_add(static_cast<long long>(pushedSamples), std::memory_order_relaxed);
                                    guiP25AudioDecodedFrames.fetch_add(static_cast<long long>(p25Audio.decodedFrames), std::memory_order_relaxed);
                                    guiP25AudioAcceptedAmbeFrames.fetch_add(static_cast<long long>(p25Audio.phase2AmbeAcceptedFrames), std::memory_order_relaxed);
                                    const qint64 speakerNowMs = QDateTime::currentMSecsSinceEpoch();
                                    guiP25AudioLastOutputMs.store(speakerNowMs, std::memory_order_relaxed);
                                    // Always refresh speaker/sustain on a real ring push (see
                                    // p25Phase2ShouldPreserveLivePlaybackBuffers / 20260720_080118).
                                    gP25AudioLastSpeakerOutputMs.store(speakerNowMs, std::memory_order_relaxed);
                                    p25Phase2UpdateSessionSustainState(rx, p25Audio, speakerNowMs, true);
                                    if (p25Phase2SpeakerOutputCanRefreshFollowActivity(p25Audio)) {
                                        p25AutoFollowLastActiveMs = std::max(p25AutoFollowLastActiveMs, speakerNowMs);
                                    }
                                    const quint32 tgLog = p25Audio.talkgroupId;
                                    const double targetFreqLog = p25Audio.effectiveTargetFreqHz > 0.0
                                        ? p25Audio.effectiveTargetFreqHz
                                        : demodFreq;
                                    const QString slotLog = rx.p25VoiceTdmaSlotKnown
                                        ? QString::number(rx.p25VoiceTdmaSlot & 0x01u)
                                        : QStringLiteral("unknown");
                                    const qulonglong generationLog =
                                        static_cast<qulonglong>(rx.p25TrafficGeneration);
                                    const QString gateLog = QString::fromStdString(p25Audio.phase2SpeakerGateReason);
                                    const QString essLog = p25Audio.phase2EssKnown
                                        ? (p25Audio.phase2EssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
                                        : QStringLiteral("unknown");
                                    const QString actionLog = QString::fromStdString(p25Audio.phase2SecurityGateAction);
                                    const QString targetEssLog = p25Audio.phase2TargetEssKnown
                                        ? (p25Audio.phase2TargetEssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
                                        : QStringLiteral("unknown");
                                    const QString targetSessionLog = p25Audio.phase2TargetSessionAudioRelease
                                        ? QStringLiteral("yes")
                                        : QStringLiteral("no");
                                    const QString targetPttLog = p25Audio.phase2TargetSecurityStateFromPtt
                                        ? QStringLiteral("yes")
                                        : QStringLiteral("no");
                                    const QString sourceLog = rx.p25VoiceSourceId != 0
                                        ? p25HexId(rx.p25VoiceSourceId, 6)
                                        : QStringLiteral("unknown");
                                    const qulonglong callSessionLog = static_cast<qulonglong>(rx.p25CurrentCallSessionId);
                                    const qlonglong grantEpochLog = static_cast<qlonglong>(rx.p25VoiceGrantEpochMs);
                                    const qulonglong pttGenerationLog = static_cast<qulonglong>(rx.p25PttGeneration);
                                    const qulonglong decodedLog = static_cast<qulonglong>(p25Audio.decodedFrames);
                                    const qulonglong targetVcwLog = static_cast<qulonglong>(p25Audio.phase2TargetVoiceCodewords);
                                    const qulonglong oppVcwLog = static_cast<qulonglong>(p25Audio.phase2OppositeVoiceCodewords);
                                    const qulonglong contextVcwLog = static_cast<qulonglong>(p25Audio.phase2ContextVoiceCodewords);
                                    const qulonglong contextDropLog = static_cast<qulonglong>(p25Audio.phase2ContextSuppressedVoiceCodewords);
                                    const qulonglong macValidLog = static_cast<qulonglong>(p25Audio.phase2MacCrcValid);
                                    const qulonglong macTotalLog = static_cast<qulonglong>(p25Audio.phase2MacPdus);
                                    const qulonglong probeAcceptedLog = static_cast<qulonglong>(p25Audio.phase2DiagnosticAmbeProbeAccepted);
                                    const qulonglong probeAttemptsLog = static_cast<qulonglong>(p25Audio.phase2DiagnosticAmbeProbeAttempts);
                                    QTimer::singleShot(0, this, [this, tgLog, targetFreqLog, slotLog, generationLog,
                                                                 pushedSamples, activeCount, queuedAfter, fillAfter, underrunsAfter,
                                                                 gateLog, essLog, actionLog, targetEssLog, targetSessionLog, targetPttLog,
                                                                 sourceLog, callSessionLog, grantEpochLog, pttGenerationLog,
                                                                 decodedLog, targetVcwLog, oppVcwLog,
                                                                 contextVcwLog, contextDropLog,
                                                                 macValidLog, macTotalLog, probeAcceptedLog, probeAttemptsLog]() {
                                        appendP25LogLineKeyed(QString("p25-audio-output:%1").arg(tgLog),
                                            QString("P25 audio output: TG=%1 target=%2MHz slot=%3 gen=%4 pushed=%5 samples gate=%6 decoded=%7 targetVcw=%8 oppVcw=%9 ctxVcw=%10 ctxDrop=%11 p2mac=%12/%13 probe=%14/%15 ess=%16 targetEss=%17 targetSession=%18 targetPtt=%19 action=%20 activeOutputs=%21 ringQueued=%22 ringFill=%23% underruns=%24 src=%25 call=%26 grantEpoch=%27 pttGen=%28.")
                                                .arg(tgLog)
                                                .arg(targetFreqLog / 1e6, 0, 'f', 5)
                                                .arg(slotLog)
                                                .arg(generationLog)
                                                .arg(static_cast<qulonglong>(pushedSamples))
                                                .arg(gateLog)
                                                .arg(decodedLog)
                                                .arg(targetVcwLog)
                                                .arg(oppVcwLog)
                                                .arg(contextVcwLog)
                                                .arg(contextDropLog)
                                                .arg(macValidLog)
                                                .arg(macTotalLog)
                                                .arg(probeAcceptedLog)
                                                .arg(probeAttemptsLog)
                                                .arg(essLog)
                                                .arg(targetEssLog)
                                                .arg(targetSessionLog)
                                                .arg(targetPttLog)
                                                .arg(actionLog)
                                                .arg(static_cast<qulonglong>(activeCount))
                                                .arg(static_cast<qulonglong>(queuedAfter))
                                                .arg(fillAfter, 0, 'f', 2)
                                                .arg(underrunsAfter)
                                                .arg(sourceLog)
                                                .arg(callSessionLog)
                                                .arg(grantEpochLog)
                                                .arg(pttGenerationLog),
                                            150);
                                    });
                                }
                                }
                            } else if (!monP25VoiceDecode) {
                                // Analog demod: push PCM directly. Only trim when the ring is badly
                                // behind (emergency). Trimming every block caused helicopter chop.
                                if (audioOutputEngine) {
                                    const size_t queued = audioOutputEngine->getRingQueuedSamples();
                                    const size_t emergencyCap = static_cast<size_t>(
                                        std::clamp(orate * 0.350, 8000.0, orate * 0.500));
                                    const size_t softTarget = static_cast<size_t>(
                                        std::clamp(orate * 0.080, 2400.0, orate * 0.120));
                                    if (queued > emergencyCap)
                                        audioOutputEngine->trimQueuedAudio(softTarget, rxAudioOutputs);
                                }
                                if (audioOutputEngine && !ch.empty()) {
                                    // Bypass 5 ms frame chunking for live NFM — fewer edge clicks.
                                    auto& pending = p25SpeakerPendingFor(pendingAudioByRx, rx).samples;
                                    if (!pending.empty()) {
                                        // Flush any leftover alignment crumbs, then go direct.
                                        pending.insert(pending.end(), ch.begin(), ch.end());
                                        audioOutputEngine->pushAudioToActiveOutputs(
                                            pending.data(), pending.size(), rxAudioOutputs);
                                        pending.clear();
                                    } else {
                                        audioOutputEngine->pushAudioToActiveOutputs(
                                            ch.data(), ch.size(), rxAudioOutputs);
                                    }
                                }
                            }
                        } else if (haveP25Audio && monP25VoiceDecode) {
                            const quint32 tgLog = p25Audio.talkgroupId;
                            const size_t samplesLog = ch.size();
                            QTimer::singleShot(0, this, [this, tgLog, samplesLog]() {
                                appendP25LogLineKeyed(QString("p25-audio-output-missing:%1").arg(tgLog),
                                    QString("P25 audio output blocked: TG=%1 decoded=%2 samples but no active playback output.")
                                        .arg(tgLog)
                                        .arg(static_cast<qulonglong>(samplesLog)),
                                    1000);
                            });
                        }
                    }
                    didWork = true;
                }
                for (int drainPass = 0; drainPass < 6; ++drainPass) {
                    if (!drainP25VoiceResults()) break;
                    didWork = true;
                }
                // Idle top-up must never open miniaudio / create AudioEngine.  The DSP
                // worker starts mid-MainWindow construction; calling ensureAudioOutputActive
                // here raced show()/exec() and produced intermittent open crashes
                // (heap corruption / AV) plus statusBar invokeMethod on a half-built UI.
                if (AudioEngine* speakerEngine = peekAudioEngineIfReady()) {
                    size_t realTopUpPushed = 0;
                    size_t bridgeTopUpPushed = 0;
                    const size_t topUpPushed = p25TopUpSpeakerPlaybackRing(
                        speakerEngine,
                        pendingAudioByRx,
                        receiverSessionStillActive,
                        &realTopUpPushed,
                        &bridgeTopUpPushed);
                    if (realTopUpPushed > 0) {
                        const qint64 topUpNowMs = QDateTime::currentMSecsSinceEpoch();
                        guiP25AudioLastOutputMs.store(topUpNowMs, std::memory_order_relaxed);
                        guiP25AudioOutputEvents.fetch_add(1, std::memory_order_relaxed);
                        guiP25AudioOutputSamples.fetch_add(static_cast<long long>(realTopUpPushed), std::memory_order_relaxed);
                        p25AutoFollowLastActiveMs = std::max(p25AutoFollowLastActiveMs, topUpNowMs);
                    }
                    if (topUpPushed > 0) {
                        const size_t queuedAfter = speakerEngine->getRingQueuedSamples();
                        const double fillAfter = speakerEngine->getRingFillPercent();
                        const int underrunsAfter = speakerEngine->getUnderrunCount();
                        QTimer::singleShot(0, this, [this, realTopUpPushed, bridgeTopUpPushed,
                                                      queuedAfter, fillAfter, underrunsAfter]() {
                            appendP25LogLineKeyed(QStringLiteral("p25-audio-top-up:idle"),
                                QString("P25 audio top-up: real=%1 bridge=%2 ringQueued=%3 ringFill=%4% underruns=%5.")
                                    .arg(static_cast<qulonglong>(realTopUpPushed))
                                    .arg(static_cast<qulonglong>(bridgeTopUpPushed))
                                    .arg(static_cast<qulonglong>(queuedAfter))
                                    .arg(fillAfter, 0, 'f', 2)
                                    .arg(underrunsAfter),
                                150);
                        });
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    didWork ? (p25Phase2SpeakerSustainDecodeActive() ? 2 : 3) : 12));
            }
        });
}
