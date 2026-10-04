#include <catch2/catch_test_macros.hpp>
#ifdef HAVE_SOAPYSDR
#include "SdrplayControlFixture.h"
#include "WorkflowRadioSession.h"
#include <SoapySDR/Registry.hpp>
#include <QCoreApplication>
#include <QStandardPaths>
#include <future>

class WorkflowControlFixture : public SdrplayControlFixture {
public:
    static inline std::mutex gateMutex;
    static inline std::condition_variable gate;
    static inline bool entered = false, resume = false;
    static inline std::atomic<bool> armed{false};
    void setAntenna(int direction, size_t channel, const std::string& name) override {
        if (name == "blocked-test" && armed.load()) {
            std::unique_lock lock(gateMutex);
            entered = true; gate.notify_all();
            gate.wait(lock, [] { return resume; });
        }
        SdrplayControlFixture::setAntenna(direction, channel, name);
    }
};

TEST_CASE("Five independent RF workflows retain ownership through real manager lifecycle", "[.ownership-live]") {
    int argc=1; char name[]="workflow-tests"; char* argv[]={name,nullptr};
    QCoreApplication app(argc,argv); QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("SDRTownTests"); app.setApplicationName("WorkflowRadioSession");
    SoapySDR::Registry registry("workflow_fixture",
        [](const SoapySDR::Kwargs&)->SoapySDR::KwargsList {
            SoapySDR::KwargsList list;
            for(int i=0;i<5;++i) list.push_back({{"driver","workflow_fixture"},{"label","Workflow test "+std::to_string(i)},{"serial","workflow-"+std::to_string(i)}});
            return list;
        }, [](const SoapySDR::Kwargs&)->SoapySDR::Device* { return new WorkflowControlFixture; }, SOAPY_SDR_ABI_VERSION);
    auto& manager=DeviceManager::instance();
    const auto devices=manager.enumerateDevices(false);
    std::vector<size_t> indices;
    for(size_t i=0;i<devices.size();++i) if(devices[i].driver=="workflow_fixture") indices.push_back(i);
    REQUIRE(indices.size()==5);
    std::string error;
    CHECK(manager.resolveWorkflowDevice(DeviceOwnership::Owner::Aircraft,"missing-radio",indices[0],&error)==size_t(-1));
    CHECK(error.find("unavailable")!=std::string::npos);
    CHECK_FALSE(manager.claimDevice(indices[0],DeviceOwnership::Owner::P25,"stale-selection",&error,"different-radio"));
    auto assignments=manager.workflowAssignments();
    for(size_t i=0;i<indices.size();++i) assignments[devices[indices[i]].stableKey]=i==4?DeviceOwnership::Owner::Sstv:DeviceOwnership::Owner::P25;
    REQUIRE(manager.setWorkflowAssignments(assignments,&error));
    std::vector<std::unique_ptr<WorkflowRadioSession>> radios;
    for(size_t i=0;i<indices.size();++i)
        radios.push_back(std::make_unique<WorkflowRadioSession>(manager,devices[indices[i]].stableKey,
            i==4?DeviceOwnership::Owner::Sstv:DeviceOwnership::Owner::P25,145.8e6,[] {return false;}));
    for(const auto& radio:radios) { CHECK(radio->valid()); CHECK(manager.isHardwareStreaming(radio->deviceIndex())); }
    const auto sstvIndex=indices[4];
    CHECK(manager.setCenterFreq(sstvIndex,420.35e6,DeviceOwnership::Owner::P25)==0);
    CHECK(manager.getCurrentCenterFreq(sstvIndex)==145.8e6);
    manager.stopStreaming(sstvIndex);
    CHECK_FALSE(radios[4]->valid());
    const auto newer=manager.claimDevice(sstvIndex,DeviceOwnership::Owner::Sstv,"replacement",&error);
    REQUIRE(newer);
    radios[4].reset(); // Old worker completion cannot stop/release the replacement.
    CHECK(manager.ownsDevice(newer));
    CHECK_FALSE(manager.restoreDevice(DeviceOwnership::Token{sstvIndex,newer.generation,newer.id-1,newer.owner,"old"},true,true,98.1e6));
    CHECK(manager.ownsDevice(newer));
    REQUIRE(manager.releaseDevice(newer));
    for(size_t i=0;i<4;++i) CHECK(radios[i]->valid());
    radios.clear();
    for(auto i:indices) CHECK_FALSE(manager.isStreaming(i));

    // Two satellite controllers may reuse separate live, unmanaged streams;
    // ending one may not alter the other's ownership or restore target.
    REQUIRE(manager.setWorkflowAssignments({}, &error));
    const auto first = indices[0], second = indices[1];
    WorkflowRadioSession listenA(manager, devices[first].stableKey, DeviceOwnership::Owner::Listen, 98.1e6, [] { return false; });
    WorkflowRadioSession listenB(manager, devices[second].stableKey, DeviceOwnership::Owner::Listen, 145.8e6, [] { return false; });
    // Explicit administrative stop invalidates the old owner before reopening.
    manager.stopStreaming(first); manager.stopStreaming(second);
    auto a = manager.claimDevice(first, DeviceOwnership::Owner::Inmarsat, "inmarsat", &error);
    auto b = manager.claimDevice(second, DeviceOwnership::Owner::Satcom, "satcom", &error);
    REQUIRE(a); REQUIRE(b);
    CHECK_FALSE(manager.startStreaming(first,true));
    REQUIRE(manager.configureDeviceCapture(a,2.048e6,0,&error));
    REQUIRE(manager.startDevice(a,&error)); REQUIRE(manager.startDevice(b,&error));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while ((!manager.isHardwareStreaming(first)||!manager.isHardwareStreaming(second)) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(manager.isHardwareStreaming(first)); REQUIRE(manager.isHardwareStreaming(second));
    while ((manager.deviceControlBusy(first) || manager.deviceControlBusy(second)) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE_FALSE(manager.deviceControlBusy(first)); REQUIRE_FALSE(manager.deviceControlBusy(second));
    const auto ownedSettings = manager.getDevices()[first];
    manager.setLiveGain(first, ownedSettings.gain + 1);
    manager.setFrequencyCorrection(first, ownedSettings.frequencyCorrectionPpm + 1);
    manager.updateDeviceParams(first, ownedSettings.sampleRate + 1000,
        ownedSettings.gain + 2, "unauthorized", ownedSettings.frequencyCorrectionPpm + 2);
    CHECK_FALSE(manager.setLiveAntenna(first, "unauthorized", &error));
    const auto afterRejectedSettings = manager.getDevices()[first];
    CHECK(afterRejectedSettings.gain == ownedSettings.gain);
    CHECK(afterRejectedSettings.sampleRate == ownedSettings.sampleRate);
    CHECK(afterRejectedSettings.antenna == ownedSettings.antenna);
    CHECK(afterRejectedSettings.frequencyCorrectionPpm == ownedSettings.frequencyCorrectionPpm);
    CHECK_FALSE(manager.applyLiveSampleRate(first, 4e6, &error));
    CHECK_FALSE(manager.setEnabled(first, !ownedSettings.enabled, &error));
    CHECK(manager.getDevices()[first].enabled == ownedSettings.enabled);
    REQUIRE(manager.setEnabled(first, ownedSettings.enabled, &error, &a));
    CHECK_FALSE(manager.setDirectSampling(first, 0, &error));
    CHECK_FALSE(manager.setRtlBiasT(first, false, &error));
    CHECK_FALSE(manager.setLiveAgc(first, true, &error));
    CHECK_FALSE(manager.setLiveGainElement(first, "RFGR", 2, &error));
    CHECK_FALSE(manager.setLiveBandwidth(first, 300000, &error));
    CHECK_FALSE(manager.setLiveSdrplaySetting(first, "biasT_ctrl", "true", &error));
    CHECK_FALSE(manager.correctWorkflowFrequency(first, DeviceOwnership::Owner::P25, 3, &error));
    REQUIRE(manager.setFrequencyCorrection(first, 0, &error, &a));
    REQUIRE(manager.setLiveGain(first, ownedSettings.gain, &error, &a));
    CHECK_FALSE(manager.setLiveGain(second, ownedSettings.gain, &error, &a));
    CHECK_FALSE(manager.configureDeviceCapture(a,2.4e6,0,&error));
    CHECK_FALSE(manager.claimDevice(first,DeviceOwnership::Owner::Satcom,"steal",&error,devices[first].stableKey,true));
    REQUIRE(manager.restoreDevice(a,true,false,98.1e6));
    CHECK(manager.isHardwareStreaming(first)); CHECK(manager.getCurrentCenterFreq(first)==98.1e6);
    CHECK(manager.ownsDevice(b)); CHECK(manager.isHardwareStreaming(second));
    const auto reuse=manager.claimDevice(first,DeviceOwnership::Owner::Inmarsat,"reuse",&error,devices[first].stableKey,true);
    REQUIRE(reuse); REQUIRE(manager.startDevice(reuse,&error,true));
    CHECK_FALSE(manager.restoreDevice(a,true,true,1546e6));
    CHECK(manager.ownsDevice(reuse)); CHECK(manager.getCurrentCenterFreq(first)==98.1e6);
    WorkflowControlFixture::armed.store(true);
    auto setting = std::async(std::launch::async, [&] {
        std::string ownError;
        return manager.setLiveAntenna(first, "blocked-test", &ownError, &reuse);
    });
    bool entered;
    {
        std::unique_lock lock(WorkflowControlFixture::gateMutex);
        entered = WorkflowControlFixture::gate.wait_for(lock, std::chrono::seconds(5), [] { return WorkflowControlFixture::entered; });
    }
    // Never leave a blocked mock driver behind even when an assertion fails.
    CHECK(entered);
    CHECK(manager.releaseDevice(reuse));
    CHECK_FALSE(manager.claimDevice(first, DeviceOwnership::Owner::Listen, "too-early", &error, {}, true));
    auto independent = manager.claimDevice(indices[2], DeviceOwnership::Owner::Sstv, "independent-control", &error);
    CHECK(independent);
    if (independent) CHECK(manager.releaseDevice(independent));
    auto stopping = std::async(std::launch::async, [&] { manager.stopStreaming(first); });
    CHECK(stopping.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
    {
        std::lock_guard lock(WorkflowControlFixture::gateMutex); WorkflowControlFixture::resume = true;
    }
    WorkflowControlFixture::gate.notify_all();
    CHECK(setting.get()); stopping.get();
    WorkflowControlFixture::armed.store(false);
    CHECK_FALSE(manager.isStreaming(first));
    auto replacement = manager.claimDevice(first, DeviceOwnership::Owner::Sstv, "replacement-after-control", &error);
    REQUIRE(replacement);
    CHECK_FALSE(manager.setLiveGain(first, 20, &error, &reuse));
    CHECK_FALSE(manager.setFrequencyCorrection(first, 2, &error, &reuse));
    CHECK(manager.releaseDevice(replacement));
    CHECK(manager.stopDevice(b));

    // The existing P25 controller retains its explicit role without acquiring
    // permissions to a newer named P25 instance. No IQ/voice simulation here.
    REQUIRE(manager.acquireDeviceLease(first, DeviceOwnership::Owner::P25, false, &error));
    CHECK_FALSE(manager.startStreaming(first, true));
    REQUIRE(manager.startStreaming(first, true, DeviceOwnership::Owner::P25));
    const auto p25Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!manager.isHardwareStreaming(first) && std::chrono::steady_clock::now() < p25Deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(manager.isHardwareStreaming(first));
    REQUIRE(manager.correctWorkflowFrequency(first, DeviceOwnership::Owner::P25, 1.25, &error));
    CHECK(manager.getDevices()[first].frequencyCorrectionPpm == 1.25);
    CHECK_FALSE(manager.setFrequencyCorrection(first, 7, &error));
    manager.stopStreaming(first);
    const auto namedP25 = manager.claimDevice(first, DeviceOwnership::Owner::P25, "named-p25", &error);
    REQUIRE(namedP25);
    CHECK_FALSE(manager.correctWorkflowFrequency(first, DeviceOwnership::Owner::P25, 7, &error));
    CHECK(manager.getDevices()[first].frequencyCorrectionPpm == 1.25);
    CHECK(manager.releaseDevice(namedP25));
}
#endif
