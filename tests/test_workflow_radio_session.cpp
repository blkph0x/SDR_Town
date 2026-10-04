#include <catch2/catch_test_macros.hpp>
#ifdef HAVE_SOAPYSDR
#include "SdrplayControlFixture.h"
#include "WorkflowRadioSession.h"
#include <SoapySDR/Registry.hpp>
#include <QCoreApplication>
#include <QStandardPaths>

TEST_CASE("Five independent RF workflows retain ownership through real manager lifecycle", "[.ownership-live]") {
    int argc=1; char name[]="workflow-tests"; char* argv[]={name,nullptr};
    QCoreApplication app(argc,argv); QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("SDRTownTests"); app.setApplicationName("WorkflowRadioSession");
    SoapySDR::Registry registry("workflow_fixture",
        [](const SoapySDR::Kwargs&)->SoapySDR::KwargsList {
            SoapySDR::KwargsList list;
            for(int i=0;i<5;++i) list.push_back({{"driver","workflow_fixture"},{"label","Workflow test "+std::to_string(i)},{"serial","workflow-"+std::to_string(i)}});
            return list;
        }, [](const SoapySDR::Kwargs&)->SoapySDR::Device* { return new SdrplayControlFixture; }, SOAPY_SDR_ABI_VERSION);
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
    REQUIRE(manager.configureDeviceCapture(a,2.048e6,0,&error));
    REQUIRE(manager.startDevice(a,&error)); REQUIRE(manager.startDevice(b,&error));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while ((!manager.isHardwareStreaming(first)||!manager.isHardwareStreaming(second)) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(manager.isHardwareStreaming(first)); REQUIRE(manager.isHardwareStreaming(second));
    CHECK_FALSE(manager.configureDeviceCapture(a,2.4e6,0,&error));
    CHECK_FALSE(manager.claimDevice(first,DeviceOwnership::Owner::Satcom,"steal",&error,devices[first].stableKey,true));
    REQUIRE(manager.restoreDevice(a,true,false,98.1e6));
    CHECK(manager.isHardwareStreaming(first)); CHECK(manager.getCurrentCenterFreq(first)==98.1e6);
    CHECK(manager.ownsDevice(b)); CHECK(manager.isHardwareStreaming(second));
    const auto reuse=manager.claimDevice(first,DeviceOwnership::Owner::Inmarsat,"reuse",&error,devices[first].stableKey,true);
    REQUIRE(reuse); REQUIRE(manager.startDevice(reuse,&error,true));
    CHECK_FALSE(manager.restoreDevice(a,true,true,1546e6));
    CHECK(manager.ownsDevice(reuse)); CHECK(manager.getCurrentCenterFreq(first)==98.1e6);
    CHECK(manager.stopDevice(reuse)); CHECK(manager.stopDevice(b));
}
#endif
