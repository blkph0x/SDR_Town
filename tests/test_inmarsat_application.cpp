#include "InmarsatAcarsApplication.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdint>
#include <vector>
#include <QByteArray>
#include "InmarsatMessageStore.h"

namespace {
// Independent synthetic MIAM v1 acknowledgement, not private field text.
// Header fields per libacars v2.2.1 miam-core.c::v1_ack_parse.
std::string encode85(std::vector<uint8_t> bytes) {
    while (bytes.size() % 4) bytes.push_back(0);
    std::string encoded;
    for (size_t i = 0; i < bytes.size(); i += 4) {
        uint32_t value = (uint32_t(bytes[i]) << 24) | (uint32_t(bytes[i+1]) << 16) |
            (uint32_t(bytes[i+2]) << 8) | bytes[i+3];
        std::array<char, 5> digits{};
        for (int j = 4; j >= 0; --j) { digits[j] = '!' + value % 85; value /= 85; }
        encoded.append(digits.data(), digits.size());
    }
    return encoded;
}
std::string ack() {
    return "T.0" + encode85({0x11,0,0,20,'.','N','0','0','0','0','1',14,0,0,0,0,0,0,0,0}) + "|";
}
std::string data(bool compressed, bool badCrc = false) {
    const std::string text = "SDR TOWN TEST MESSAGE";
    uint32_t crc = 0xffffffffu;
    for (unsigned char c : text) {
        crc ^= uint32_t(c) << 24;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc << 1) ^ ((crc & 0x80000000u) ? 0x04c11db7u : 0u);
    }
    crc = ~crc;
    if (badCrc) crc ^= 1;
    std::vector<uint8_t> body(text.begin(), text.end());
    if (compressed) {
        // RFC 1951 final stored block, independently generated raw DEFLATE.
        const auto len = static_cast<uint16_t>(body.size());
        body.insert(body.begin(), {1, uint8_t(len), uint8_t(len >> 8), uint8_t(~len), uint8_t((~len) >> 8)});
    }
    const auto pduSize = 20 + body.size();
    std::vector<uint8_t> header{1,0,0,uint8_t(pduSize),'.','N','0','0','0','0','1',2,0,
        uint8_t(compressed ? 0x40 : 0),'F','R',uint8_t(crc >> 24),uint8_t(crc >> 16),uint8_t(crc >> 8),uint8_t(crc)};
    return "T" + std::to_string((4 - body.size() % 4) % 4) + "0" + encode85(header) + "|" + encode85(body);
}
}

TEST_CASE("Inmarsat MIAM encoded acknowledgement is readable without altering RF text", "[inmarsat][application]") {
    const auto raw = ack();
    const auto result = decodeInmarsatAcarsApplication("MA", raw + "\r\n");
    CHECK(result.protocol == "MIAM");
    REQUIRE(result.status == "decoded");
    CHECK(result.text.find("MIAM CORE Ack, version 1") != std::string::npos);
    CHECK(result.text.find(".N00001") != std::string::npos);
    CHECK(result.text.find("Msg ACK num: 7") != std::string::npos);
    CHECK(result.text.find("Transfer result: ack") != std::string::npos);
    CHECK(decodeInmarsatAcarsApplication("H1", raw).protocol == "MIAM");
    CHECK(decodeInmarsatAcarsApplication(std::string("_\x7f", 2), "").status == "control");
}

namespace {
std::string arinc(const std::vector<uint8_t>& bytes) {
    const std::string prefix="ADS.N00001";
    uint16_t crc=0xffff;
    const auto consume=[&](uint8_t b) {
        crc^=uint16_t(b)<<8;
        for(int i=0;i<8;++i)crc=uint16_t((crc<<1)^((crc&0x8000)?0x1021:0));
    };
    for(auto c:prefix)consume(uint8_t(c));
    for(auto c:bytes)consume(c);
    crc=uint16_t(~crc);
    auto payload=bytes;payload.push_back(uint8_t(crc>>8));payload.push_back(uint8_t(crc));
    std::string text="/TEST123."+prefix;
    constexpr char hex[]="0123456789ABCDEF";
    for(auto b:payload){text+=hex[b>>4];text+=hex[b&15];}
    return text;
}
}

TEST_CASE("ADS-C identity does not require a position and contracts never become map reports", "[inmarsat][application]") {
    const auto identity=arinc({17,0xab,0xcd,0xef});
    auto decoded=decodeInmarsatAcarsApplication("H1",identity,InmarsatMessageDirection::AirToGround);
    REQUIRE(decoded.status=="decoded");
    REQUIRE(decoded.aircraft);
    CHECK(decoded.aircraft->airframeId==0xabcdef);
    CHECK_FALSE(decoded.hasPosition);
    const auto position=arinc({7,0,0,0,0,0,0,0,0,0,0});
    const auto down=decodeInmarsatAcarsApplication("H1",position,InmarsatMessageDirection::AirToGround);
    REQUIRE(down.aircraft); CHECK(down.hasPosition);
    CHECK(down.aircraft->latitude==0); CHECK(down.aircraft->longitude==0);
    CHECK_FALSE(decodeInmarsatAcarsApplication("H1",position,InmarsatMessageDirection::GroundToAir).aircraft);
    CHECK_FALSE(decodeInmarsatAcarsApplication("H1",position).aircraft);
    auto corrupt=position;corrupt.back()=corrupt.back()=='0'?'1':'0';
    const auto bad=decodeInmarsatAcarsApplication("H1",corrupt,InmarsatMessageDirection::AirToGround);
    CHECK(bad.status=="invalid");CHECK_FALSE(bad.aircraft);
    for(size_t n=1;n<position.size();++n) {
        auto truncated=decodeInmarsatAcarsApplication("H1",position.substr(0,n),InmarsatMessageDirection::AirToGround);
        CHECK_FALSE(truncated.aircraft);
    }
    auto hexBad=position;hexBad.back()='Z';
    CHECK(decodeInmarsatAcarsApplication("H1",hexBad,InmarsatMessageDirection::AirToGround).status=="invalid");
    const auto wrapped=decodeInmarsatAcarsApplication("H1","F01ATEST01"+identity,
        InmarsatMessageDirection::AirToGround,true);
    REQUIRE(wrapped.aircraft);CHECK(wrapped.aircraft->airframeId==0xabcdef);
}

TEST_CASE("ADS-C estimates use earth reference not heading or Mach", "[inmarsat][application]") {
    // libacars tag 14 and tag 15 share bit layout, not physical interpretation.
    std::vector<uint8_t> groups{7,0,0,0,0,0,0,0,0,0,0,14,0,0,0,0,0};
    auto result=decodeInmarsatAcarsApplication("H1",arinc(groups),InmarsatMessageDirection::AirToGround);
    REQUIRE(result.aircraft);CHECK(result.aircraft->hasGroundVector);
    CHECK(result.aircraft->groundSpeedKnots==0);
    groups[11]=15;result=decodeInmarsatAcarsApplication("H1",arinc(groups),InmarsatMessageDirection::AirToGround);
    REQUIRE(result.aircraft);CHECK_FALSE(result.aircraft->hasGroundVector);
    groups[11]=14;groups[12]=0x80;
    result=decodeInmarsatAcarsApplication("H1",arinc(groups),InmarsatMessageDirection::AirToGround);
    REQUIRE(result.aircraft);CHECK_FALSE(result.aircraft->hasGroundVector);
}

TEST_CASE("Independent CPDLC and media advisory applications produce text not invented positions", "[inmarsat][application]") {
    // Public libacars v2.2.1 PROG_GUIDE example 3, expected uplink CONTACT.
    const std::string raw="/AKLCDYA.AT1.9V-SVG21D0755D84AD067448398722949A7521C8AB4A1C8EAB5CE393";
    auto r=decodeInmarsatAcarsApplication("AA",raw,InmarsatMessageDirection::GroundToAir);
    REQUIRE(r.status=="decoded");CHECK(r.protocol=="CPDLC");
    CHECK(r.text.find("CHRISTCHURCH")!=std::string::npos);
    CHECK(r.text.find("128.100")!=std::string::npos);CHECK_FALSE(r.aircraft);
    CHECK(decodeInmarsatAcarsApplication("AA",raw).status=="unsupported");
    r=decodeInmarsatAcarsApplication("SA","0EX120000XV/TEST");
    CHECK(r.status=="decoded");CHECK(r.protocol=="Media advisory");
    CHECK(r.text.find("Inmarsat Aero")!=std::string::npos);
    CHECK(decodeInmarsatAcarsApplication("SA","0EX990000X").status=="invalid");
    r=decodeInmarsatAcarsApplication("ZZ","AIRLINE TEXT");
    CHECK(r.status=="uninterpreted");CHECK(r.text=="AIRLINE TEXT");
    r=decodeInmarsatAcarsApplication("H1","TEXT FROM AIRLINE");
    CHECK(r.status=="uninterpreted");CHECK(r.text=="TEXT FROM AIRLINE");
    CHECK(decodeInmarsatAcarsApplication("MA","S001001.0uuuuu|").status=="unsupported");
}

TEST_CASE("OHMA compressed JSON is interpreted without treating multipart text as complete", "[inmarsat][application]") {
    const auto message=[](const QByteArray& json) {
        // qCompress's length prefix is not RFC1950; remove it before BASE64.
        return "/O2.OHMA"+qCompress(json).mid(4).toBase64().toStdString();
    };
    auto r=decodeInmarsatAcarsApplication("H1",message(R"({"version":"1","message":"TEST DIAGNOSTIC"})"));
    REQUIRE(r.status=="decoded"); CHECK(r.protocol=="OHMA");
    CHECK(r.text.find("TEST DIAGNOSTIC")!=std::string::npos);CHECK_FALSE(r.aircraft);
    r=decodeInmarsatAcarsApplication("H1",message(R"({"version":"1","message":"part","convo_id":"test","msg_seq":1,"msg_total":2})"));
    CHECK(r.status=="unsupported");
    r=decodeInmarsatAcarsApplication("H1",message("not JSON"));
    CHECK(r.status=="invalid");
}

TEST_CASE("Later aircraft identity enriches map without refreshing an old location", "[inmarsat][application]") {
    InmarsatMessageStore store;
    InmarsatMessage m;m.kind=InmarsatMsgKind::Acars;m.validated=true;m.aesId=0xabcdef;
    m.unixTime=100;m.hasPosition=true;m.latDeg=-33;m.lonDeg=150;
    store.push(m);
    m.unixTime=200;m.hasPosition=false;m.icaoHex="ABCDEF";m.callsign="TEST123";
    store.push(m);
    const auto positions=store.positions();REQUIRE(positions.size()==1);
    CHECK(positions[0].callsign=="TEST123");CHECK(positions[0].icaoHex=="ABCDEF");
    CHECK(positions[0].unixTime==100);CHECK(positions[0].latDeg==-33);
    InmarsatMessageStore replay;CHECK(replay.positions().empty());
}

TEST_CASE("Inmarsat MIAM fails explicitly for malformed or unsupported application data", "[inmarsat][application]") {
    for (const auto& raw : {"T", "T.0|", "T.0~~~~~|", "T.0uuuuu|", "T.0z!!!!|", "T.0!!!!|",
        "T.0!!!!!|junk", "T44!!!!!|", "T00!!!!!|!"})
        CHECK(decodeInmarsatAcarsApplication("MA", raw).status == "invalid");
    CHECK(decodeInmarsatAcarsApplication("MA", std::string("T.0z|\0extra", 11)).status == "invalid");
    CHECK(decodeInmarsatAcarsApplication("MA", "T" + std::string(16384, 'z')).status == "invalid");
    // File-transfer request metadata is now interpreted, but is not file contents.
    CHECK(decodeInmarsatAcarsApplication("MA", "F001000100000000000000").status == "control");
    auto shortHeader = "T.0" + encode85({0x11,0,0,20}) + "|";
    CHECK(decodeInmarsatAcarsApplication("MA", shortHeader).status == "invalid");
    for (size_t n = 0; n < ack().size(); ++n) {
        const auto result = decodeInmarsatAcarsApplication("MA", ack().substr(0, n));
        CHECK(result.status != "decoded");
    }
}

TEST_CASE("MIAM data verifies body CRC after raw DEFLATE or uncompressed decoding", "[inmarsat][application]") {
    for (bool compressed : {false, true}) {
        const auto message = decodeInmarsatAcarsApplication("MA", data(compressed));
        REQUIRE(message.status == "decoded");
        CHECK(message.text.find("SDR TOWN TEST MESSAGE") != std::string::npos);
        const auto corrupt = decodeInmarsatAcarsApplication("MA", data(compressed, true));
        CHECK(corrupt.status == "invalid");
    }
}
