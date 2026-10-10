#include "SstvRfLiveSession.h"
#include "SstvRfRouter.h"
#include "SstvLiveSession.h"
#include "DeviceManager.h"
#include "Receiver.h"
#include "WorkflowRadioSession.h"
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <stdexcept>

namespace {void require(bool ok,const char* message){if(!ok) throw std::runtime_error(message);}}
nlohmann::json decodeSstvDedicatedRadio(const QString& key, double frequencyHz,
    const QString& output, const QString& imageMode, const QString& rfMode,
    const std::function<bool()>& finish, const std::function<bool()>& cancel,
    const SstvPreview& preview, const std::function<void(const QString&)>& routeStatus,
    const SstvAudioMonitor& monitor) {
    WorkflowRadioSession radio(DeviceManager::instance(), key.toStdString(),
        DeviceManager::DeviceLeaseOwner::Sstv, frequencyHz,
        [&] { return (cancel && cancel()) || (finish && finish()); });
    auto receiver = std::make_shared<Receiver>();
    receiver->deviceIndex = radio.deviceIndex(); receiver->freqHz = frequencyHz; receiver->active = true;
    return decodeSstvRfLive(receiver, output, imageMode, rfMode, finish,
        [&] { return !radio.valid() || (cancel && cancel()); }, preview, routeStatus, monitor);
}
nlohmann::json decodeSstvRfLive(const std::shared_ptr<Receiver>& receiver,
    const QString& output,const QString& imageMode,const QString& rfMode,
    const std::function<bool()>& finish,const std::function<bool()>& cancel,
    const SstvPreview& preview,const std::function<void(const QString&)>& routeStatus,
    const SstvAudioMonitor& monitor) {
    require(bool(receiver),"Start the main receiver before receiving SSTV");
    require(!QFileInfo::exists(output) && QDir(QFileInfo(output).absolutePath()).exists(),"Choose a new SSTV output folder under an existing directory");
    size_t device; double target;
    {std::lock_guard lock(receiver->stateMutex);device=receiver->deviceIndex;target=receiver->freqHz;}
    auto& manager=DeviceManager::instance();
    const auto devices=manager.getDevices();
    require(device<devices.size(),"SSTV device is unavailable");
    const auto key=devices[device].stableKey;
    const double rate=manager.getCurrentSampleRate(device),center=manager.getCurrentCenterFreq(device);
    const auto tune=manager.getCenterTuneAppliedSeq(device);
    const auto validate=[&] {
        {std::lock_guard lock(receiver->stateMutex);
         require(receiver->active, "Start the main receiver before receiving SSTV");
         require(!receiver->p25VoiceDecodeEnabled,
                 "This receiver is decoding P25 voice, so SSTV cannot share it. Choose the radio in this window, or turn off P25 voice decode, then listen in USB (HF) or NFM (VHF).");
         require(receiver->deviceIndex==device && receiver->freqHz==target,"SSTV receiver changed; restart reception");}
        const auto current=manager.getDevices();
        require(device<current.size() && current[device].stableKey==key && manager.isStreaming(device),"SSTV device stopped or changed");
        require(manager.getCurrentSampleRate(device)==rate && manager.getCurrentCenterFreq(device)==center &&
                manager.getCenterTuneAppliedSeq(device)==tune && manager.getCenterTuneRequestSeq(device)==tune,
                "SSTV device tuned or changed sample rate; restart reception");
    };
    validate();
    SstvRfRouter router(rate,center,target,rfMode.toStdString(),device);
    Receiver cursor;
    manager.setReceiverCursorToLiveEdge(device,cursor);
    QString detected=QString::fromUtf8(router.selectedMode().data(),qsizetype(router.selectedMode().size()));
    if(routeStatus) routeStatus(detected);
    const auto play=[&](const SstvInputEvent& audio) {
        if(!monitor || audio.count==0) return;
        // Dedicated SSTV owns the radio, so the main speaker path stays quiet.
        // A P25 control mute still silences NFM/WFM on a shared receiver; play
        // the SSTV demod then. USB/LSB stay on the main path, so do not play twice.
        bool playMonitor=manager.deviceLeaseOwner(device)==DeviceManager::DeviceLeaseOwner::Sstv;
        if(!playMonitor) {
            std::lock_guard lock(receiver->stateMutex);
            playMonitor=receiver->p25ControlChannelMute &&
                (receiver->mode==DemodMode::NFM || receiver->mode==DemodMode::WFM);
        }
        if(playMonitor) monitor(audio.samples.data(),audio.count,audio.sampleRate);
    };
    const auto result=decodeSstvStream([&]()->SstvStreamItem {
        validate();
        if(auto audio=router.pop()) {play(*audio); return *audio;}
        if(finish && finish()) return SstvStreamEnd{};
        auto iq=manager.getNewIQWindowForReceiver(device,cursor,router.maxInputSamples());
        validate(); // A concurrent retune must not relabel old IQ with a new center.
        router.process(iq.samples,iq.startAbsolute,iq.streamEpoch,iq.cursorDiscontinuity);
        const auto now=QString::fromUtf8(router.selectedMode().data(),qsizetype(router.selectedMode().size()));
        if(now!=detected) {detected=now;if(routeStatus) routeStatus(now);}
        if(auto audio=router.pop()) {play(*audio); return *audio;}
        return SstvStreamIdle{};
    },imageMode,cancel,preview,output);
    require(!cancel || !cancel(),"SSTV live session cancelled");
    return saveSstvLiveResult(result,output,{{"rfModeRequested",rfMode.toStdString()},
        {"rfModeSelected",detected.toStdString()},{"rfAutoEvidence",rfMode=="auto" && detected!="searching"?"classic-VIS-parity":"manual-or-no-lock"}});
}
