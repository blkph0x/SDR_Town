#include "InmarsatTracking.h"
#include "InmarsatIdentity.h"
#include "InmarsatDiagnostics.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <limits>
#include <cmath>
#include <array>
#include <algorithm>

namespace {
constexpr uint32_t id=0x123456;
constexpr double now=10000,utc=1800000000;
InmarsatAircraft aircraft(bool position=true) {
    InmarsatAircraft a;a.lastSeenMonotonic=now;a.positionMonotonic=now;a.positionTime=utc;
    auto& m=a.identity;m.aesId=id;m.validated=true;m.classicAeroIdentity=true;m.icaoHex="123456";
    m.hasPosition=position;m.latDeg=-34;m.lonDeg=151;m.altitudeFt=35000;m.unixTime=utc;
    return a;
}
nlohmann::json reply(double seen=0) {
    return {{"now",utc*1000},{"ac",{{{"hex","123456"},{"lat",-33.0},{"lon",150.0},{"seen_pos",seen},{"gs",400},{"track",90}}}}};
}
}
TEST_CASE("Classic Aero ICAO formatting requires a real 24-bit address", "[inmarsat][tracking]") {
    CHECK(inmarsatClassicIcao(0x1234)=="001234");
    CHECK(inmarsatClassicIcao(0xabcdef)=="ABCDEF");
    for(uint32_t invalid:{0u,0xffffffu,0x1000000u,0xffffffffu})CHECK(inmarsatClassicIcao(invalid).empty());
}
TEST_CASE("Validated C identity creates an aircraft without inventing a position or speech", "[inmarsat][tracking]") {
    std::array<uint8_t,12> su{0x30,0x12,0x34,0x56,0x01};
    InmarsatMessageStore store;
    auto identity=inmarsatClassicVoiceIdentity(su);REQUIRE(identity);
    CHECK_FALSE(identity->hasPosition);store.push(*identity);
    REQUIRE(store.aircraft().size()==1);CHECK(store.aircraft()[0].identity.icaoHex=="123456");
    CHECK(store.positions().empty());
    su[0]=0x60;REQUIRE(inmarsatClassicVoiceIdentity(su));
    su[0]=0x31;CHECK_FALSE(inmarsatClassicVoiceIdentity(su));
    su[0]=0x30;CHECK_FALSE(inmarsatClassicVoiceIdentity(std::span(su).first(11)));
    su[1]=su[2]=su[3]=0;CHECK_FALSE(inmarsatClassicVoiceIdentity(su));
}
TEST_CASE("Hybrid map preserves source separation and disable restores RF", "[inmarsat][tracking]") {
    InmarsatTracking model;model.setRf({aircraft()},now);
    CHECK_FALSE(model.acceptOnline(reply(),{id},now,utc));
    model.setOnlineEnabled(true);REQUIRE(model.acceptOnline(reply(),{id},now,utc));
    auto r=model.report({},now,false);REQUIRE(r["positions"].size()==1);
    CHECK(r["positions"][0]["positionSource"]=="adsb_lol");
    CHECK(r["positions"][0]["latDeg"]==-33);CHECK_FALSE(r["positions"][0]["hasAltitude"].get<bool>());
    model.setOnlineEnabled(false);r=model.report({},now,false);
    CHECK(r["positions"][0]["positionSource"]=="rf_adsc");CHECK(r["positions"][0]["latDeg"]==-34);
    model.setOnlineEnabled(true);CHECK(model.report({},now,false)["positions"][0]["positionSource"]=="rf_adsc");
}
TEST_CASE("Map age is monotonic and identity traffic never renews position TTL", "[inmarsat][tracking]") {
    InmarsatTracking model;auto a=aircraft();model.setRf({a},now);
    CHECK(model.report({},now+1199,false)["positions"].size()==1);
    a.lastSeenMonotonic=now+1200;a.identity.unixTime=utc-50000;
    model.setRf({a},now+1200);
    CHECK(model.report({},now+1200,false)["positions"].empty());
    CHECK(model.report({},now+1200,false)["aircraft"][0]["reason"]=="position_expired");
    CHECK(model.eligibleIds(now+2400).empty());
}
TEST_CASE("Online rows must match requested currently received identities", "[inmarsat][tracking]") {
    InmarsatTracking model;model.setOnlineEnabled(true);auto a=aircraft(false);model.setRf({a},now);
    REQUIRE(model.acceptOnline(reply(),{},now,utc));CHECK(model.report({},now,false)["positions"].empty());
    auto other=reply();other["ac"][0]["hex"]="~23456";
    REQUIRE(model.acceptOnline(other,{id},now,utc));CHECK(model.report({},now,false)["positions"].empty());
    a.identity.classicAeroIdentity=false;model.setRf({a},now);
    REQUIRE(model.acceptOnline(reply(),{id},now,utc));CHECK(model.report({},now,false)["positions"].empty());
    a.identity.classicAeroIdentity=true;a.identity.icaoHex="654321";model.setRf({a},now);CHECK(model.eligibleIds(now).empty());
    a.identity.icaoHex="123456";model.setRf({a},now);
    CHECK(model.eligibleIds(now+1200).empty());
    REQUIRE(model.acceptOnline(reply(),{id},now,utc));model.setRf({},now+1);
    CHECK(model.report({},now+1,false)["positions"].empty());
}
TEST_CASE("Online parser rejects malformed stale future and nonfinite data", "[inmarsat][tracking]") {
    InmarsatTracking model;model.setOnlineEnabled(true);model.setRf({aircraft(false)},now);
    for(const auto& malformed:{nlohmann::json{},nlohmann::json::array(),nlohmann::json{{"now",utc},{"ac",nlohmann::json::array()}},
        nlohmann::json{{"now",(utc+10)*1000},{"ac",nlohmann::json::array()}}})
        CHECK_FALSE(model.acceptOnline(malformed,{id},now,utc));
    for(const auto& [key,value]:std::vector<std::pair<std::string,nlohmann::json>>{
        {"lat",91},{"lon",181},{"lat","-33"},{"seen_pos",-1},{"seen_pos",300},{"seen_pos","0"},
        {"lat",std::numeric_limits<double>::quiet_NaN()},{"hex","000000"},{"hex","FFFFFF"}}) {
        auto bad=reply();bad["ac"][0][key]=value;
        CHECK(model.acceptOnline(bad,{id},now,utc));CHECK(model.report({},now,false)["positions"].empty());
    }
}
TEST_CASE("Repeated provider fixes cannot refresh age and empty replies retain bounded cache", "[inmarsat][tracking]") {
    InmarsatTracking model;model.setOnlineEnabled(true);model.setRf({aircraft(false)},now);
    REQUIRE(model.acceptOnline(reply(),{id},now,utc));
    auto repeated=reply(20);repeated["now"]=(utc+20)*1000;
    REQUIRE(model.acceptOnline(repeated,{id},now+20,utc+20));
    CHECK(model.counters()["mapOnlineOlder"]==1);
    CHECK(model.report({},now+20,false)["positions"][0]["ageSeconds"]==20);
    auto empty=reply();empty["ac"]=nlohmann::json::array();
    REQUIRE(model.acceptOnline(empty,{id},now+21,utc+21));
    CHECK(model.report({},now+299,false)["positions"].size()==1);
    CHECK(model.report({},now+300,false)["positions"].empty());
}
TEST_CASE("Older online coordinates cannot displace a newer RF observation", "[inmarsat][tracking]") {
    InmarsatTracking model;model.setOnlineEnabled(true);model.setRf({aircraft()},now);
    REQUIRE(model.acceptOnline(reply(10),{id},now,utc));
    CHECK(model.report({},now,false)["positions"][0]["positionSource"]=="rf_adsc");
}
TEST_CASE("Estimates retain anchors expire and cross the date line without fake observations", "[inmarsat][tracking]") {
    auto a=aircraft();a.identity.latDeg=0;a.identity.lonDeg=179.999;
    a.identity.hasGroundVector=true;a.identity.groundTrackDeg=90;a.identity.groundSpeedKnots=400;
    InmarsatTracking model;model.setRf({a},now);
    auto p=model.report({},now+20,true)["positions"][0];
    CHECK(p["estimated"]==true);CHECK(p["measuredLonDeg"]==179.999);CHECK(p["lonDeg"].get<double>()<0);
    CHECK(p["latDeg"].get<double>()==Catch::Approx(0).margin(1e-9));
    CHECK(model.report({},now+121,true)["positions"][0]["estimated"]==false);
    CHECK(model.report({},now+20,false)["positions"][0]["lonDeg"]==179.999);
    a.identity.positionHasTimestamp=true;a.identity.positionSecondsPastHour=std::fmod(utc-600,3600.0);
    model.setRf({a},now);CHECK(model.report({},now+20,true)["positions"][0]["estimated"]==false);
}
TEST_CASE("All active call markers remain distinct from selected playback", "[inmarsat][tracking]") {
    auto a=aircraft(),b=aircraft();b.identity.aesId=0x654321;b.identity.icaoHex="654321";
    InmarsatTracking model;model.setRf({a,b},now);
    nlohmann::json report={{"voiceActive",true},{"voiceAesId",id},
        {"audio",{{"speakerRunning",true},{"audioAesId",id}}}};
    report["watch"]["channels"]={{{"decoder",{{"voiceActive",true},{"voiceAesId",0x654321}}}}};
    const auto r=model.report(report,now,false);
    CHECK(r["tracking"]["mapActiveCalls"]==2);CHECK(r["tracking"]["mapVoiceWithoutPosition"]==0);
    CHECK(r["speakerAesId"]==id);
    report["receptionRunning"]=false;CHECK(model.report(report,now,false)["activeAesIds"].empty());
    report["receptionRunning"]=true;report["voiceAesId"]=0;report.erase("watch");
    CHECK(model.report(report,now,false)["activeAesIds"].empty());
}
TEST_CASE("Map diagnostics transmit numeric evidence only", "[inmarsat][tracking]") {
    const auto safe=InmarsatDiagnostics::remotePayload({{"mapRfPositions",3},{"mapOnlineAccepted",2},
        {"mapUnlocated","private"},{"lookupLatencyMs",17},{"lookupState","private"},
        {"aircraft",{{{"aesId",id},{"latDeg",-34}}}},{"voiceAesId",id}});
    CHECK(safe.size()==3);CHECK(safe["mapRfPositions"]==3);
    CHECK_FALSE(safe.contains("aircraft"));CHECK_FALSE(safe.contains("mapUnlocated"));
}
TEST_CASE("Hybrid snapshot retains independent positions after identity eviction without age renewal", "[inmarsat][tracking]") {
    auto& store=InmarsatMessageStore::instance();store.clear();
    struct Cleanup {~Cleanup(){InmarsatMessageStore::instance().clear();}} cleanup;
    auto position=aircraft().identity;position.kind=InmarsatMsgKind::Acars;position.unixTime=10;store.push(position);
    const double receipt=store.trackingAircraft().front().positionMonotonic;
    REQUIRE(receipt>0);
    for(uint32_t i=1;i<=750;++i) {
        InmarsatMessage m;m.kind=InmarsatMsgKind::Su;m.validated=true;m.aesId=i;m.unixTime=20+i;
        store.push(m);
    }
    REQUIRE(store.aircraft().size()==256);
    const auto snapshot=store.trackingAircraft();REQUIRE(snapshot.size()==257);
    InmarsatTracking model;model.setRf(snapshot,inmarsatMonotonicSeconds());
    const auto r=model.report({},inmarsatMonotonicSeconds(),false);
    REQUIRE(r["positions"].size()==1);CHECK(r["positions"][0]["aesId"]==id);
    CHECK(model.report({},receipt+1200,false)["positions"].empty());
    const auto again=store.trackingAircraft();
    const auto retained=std::find_if(again.begin(),again.end(),[](const auto& a){return a.identity.aesId==id;});
    REQUIRE(retained!=again.end());CHECK(retained->positionMonotonic==receipt);
}
