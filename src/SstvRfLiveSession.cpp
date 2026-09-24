#include "SstvRfLiveSession.h"
#include "SstvRfRouter.h"
#include "SstvLiveSession.h"
#include "DeviceManager.h"
#include "Receiver.h"
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <stdexcept>

namespace {void require(bool ok,const char* message){if(!ok) throw std::runtime_error(message);}}
nlohmann::json decodeSstvRfLive(const std::shared_ptr<Receiver>& receiver,
    const QString& output,const QString& imageMode,const QString& rfMode,
    const std::function<bool()>& finish,const std::function<bool()>& cancel,
    const SstvPreview& preview,const std::function<void(const QString&)>& routeStatus) {
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
         require(receiver->active && !receiver->p25VoiceDecodeEnabled && !receiver->p25ControlChannelMute,
                 "SSTV requires an active non-P25 main receiver");
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
    QElapsedTimer duration; duration.start();
    const auto result=decodeSstvStream([&]()->SstvStreamItem {
        validate();
        if(auto audio=router.pop()) return *audio;
        if((finish && finish()) || duration.elapsed()>=540000) return SstvStreamEnd{};
        auto iq=manager.getNewIQWindowForReceiver(device,cursor,router.maxInputSamples());
        validate(); // A concurrent retune must not relabel old IQ with a new center.
        router.process(iq.samples,iq.startAbsolute,iq.streamEpoch,iq.cursorDiscontinuity);
        const auto now=QString::fromUtf8(router.selectedMode().data(),qsizetype(router.selectedMode().size()));
        if(now!=detected) {detected=now;if(routeStatus) routeStatus(now);}
        if(auto audio=router.pop()) return *audio;
        return SstvStreamIdle{};
    },imageMode,cancel,preview);
    require(!cancel || !cancel(),"SSTV live session cancelled");
    return saveSstvLiveResult(result,output,{{"rfModeRequested",rfMode.toStdString()},
        {"rfModeSelected",detected.toStdString()},{"rfAutoEvidence",rfMode=="auto" && detected!="searching"?"classic-VIS-parity":"manual-or-no-lock"}});
}
