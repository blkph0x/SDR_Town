#include "BandPlan.h"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <limits>
#include <thread>

namespace {
BandPlanProfile fixture() {
    return {"local-test", "ITU Region 3", "Australia", "Test location", "Test fixture only", "2026-09-17", {
        {"Voice", 100e6, 101e6, DemodMode::NFM, 12500, 3000, 12500, "Analog voice", "https://example.org/plan", 0},
        {"Data", 100.2e6, 100.3e6, DemodMode::AUTO, 0, 0, 0, "Data hint", "https://example.org/plan", 1}}};
}
}

TEST_CASE("Band-plan lookup uses half-open intervals and specific data overrides", "[bandplan]") {
    const auto p = fixture();
    REQUIRE_FALSE(lookupBand(p, 99e6));
    REQUIRE(lookupBand(p, 100e6)->mode == DemodMode::NFM);
    REQUIRE(lookupBand(p, 100.2e6)->mode == DemodMode::AUTO);
    REQUIRE(lookupBand(p, 100.3e6)->mode == DemodMode::NFM);
    REQUIRE_FALSE(lookupBand(p, 101e6));
    REQUIRE_FALSE(lookupBand(p, std::numeric_limits<double>::quiet_NaN()));
    REQUIRE_FALSE(lookupBand(p, std::numeric_limits<double>::infinity()));
}

TEST_CASE("Band-plan conflicts cannot force an arbitrary demodulator", "[bandplan]") {
    auto p = fixture();
    auto conflict = p.entries[0]; conflict.mode = DemodMode::AM;
    p.entries.push_back(conflict);
    REQUIRE(lookupBand(p, 100.1e6)->mode == DemodMode::AUTO);
    REQUIRE(lookupBand(p, 100.1e6)->bandwidthHz == 0);
    p.entries.back().priority = 2;
    REQUIRE(lookupBand(p, 100.1e6)->mode == DemodMode::AM);
}

TEST_CASE("Band-plan import is bounded validated and roundtrippable", "[bandplan]") {
    const auto encoded = serializeBandPlan(fixture());
    const auto p = parseBandPlan(encoded);
    REQUIRE(p.entries.size() == 2);
    REQUIRE(p.location == "Test location");
    REQUIRE(p.entries[0].bandwidthHz == 12500);
    auto j = nlohmann::json::parse(encoded);
    SECTION("unknown demodulator") { j["entries"][0]["mode"] = "P25"; REQUIRE_THROWS(parseBandPlan(j.dump())); }
    SECTION("reversed frequency") { j["entries"][0]["endHz"] = 1; REQUIRE_THROWS(parseBandPlan(j.dump())); }
    SECTION("invalid source") { j["entries"][0]["source"] = "javascript:alert(1)"; REQUIRE_THROWS(parseBandPlan(j.dump())); }
    SECTION("source without host") { j["entries"][0]["source"] = "https://"; REQUIRE_THROWS(parseBandPlan(j.dump())); }
    SECTION("fractional priority") { j["entries"][0]["priority"] = 0.5; REQUIRE_THROWS(parseBandPlan(j.dump())); }
    SECTION("overflow priority") { j["entries"][0]["priority"] = 4294967296ULL; REQUIRE_THROWS(parseBandPlan(j.dump())); }
    SECTION("control characters") { j["location"] = std::string("bad\0name", 8); REQUIRE_THROWS(parseBandPlan(j.dump())); }
    SECTION("invalid filter") { j["entries"][0]["lpfHz"] = -1; REQUIRE_THROWS(parseBandPlan(j.dump())); }
    SECTION("missing metadata") { j.erase("revision"); REQUIRE_THROWS(parseBandPlan(j.dump())); }
    SECTION("built-in overwrite") { j["id"] = "AU"; REQUIRE_THROWS(parseBandPlan(j.dump())); }
    SECTION("oversize") { REQUIRE_THROWS(parseBandPlan(std::string(262145, ' '))); }
    SECTION("excess nesting") { REQUIRE_THROWS(parseBandPlan(std::string(40, '[') + "0" + std::string(40, ']'))); }
}

TEST_CASE("Country selection does not leak AU CB into UK or US", "[bandplan]") {
    auto& catalog = BandPlanCatalog::instance();
    const auto original = catalog.active();
    struct Restore { std::string id; ~Restore() { BandPlanCatalog::instance().select(id); } } restore{original->id};
    REQUIRE(catalog.select("AU"));
    REQUIRE(findBandPlanForFrequency(476.4625e6)->mode == DemodMode::NFM);
    REQUIRE_FALSE(findBandPlanForFrequency(476.95e6));
    REQUIRE_FALSE(findBandPlanForFrequency(476.975e6));
    REQUIRE_FALSE(findBandPlanForFrequency(110e6));
    REQUIRE(findBandPlanForFrequency(120e6)->mode == DemodMode::AM);
    REQUIRE_FALSE(findBandPlanForFrequency(161.975e6));
    REQUIRE(catalog.select("GB"));
    REQUIRE_FALSE(findBandPlanForFrequency(476.4625e6));
    REQUIRE(lookupBand(*catalog.active(), 446.1e6)->mode == DemodMode::AUTO);
    REQUIRE(catalog.select("US"));
    REQUIRE(findBandPlanForFrequency(162.55e6)->mode == DemodMode::NFM);
    REQUIRE_FALSE(catalog.select("nonexistent"));
    REQUIRE(catalog.active()->id == "US");
}

TEST_CASE("Band-plan snapshots survive concurrent selection", "[bandplan]") {
    auto& catalog = BandPlanCatalog::instance();
    const auto before = catalog.active();
    std::atomic<bool> valid{true};
    std::thread reader([&] {
        for (int i = 0; i < 1000; ++i) {
            const auto snapshot = catalog.active();
            if (snapshot->entries.empty() || !lookupBand(*snapshot, 100e6)) valid = false;
        }
    });
    for (int i = 0; i < 1000; ++i) catalog.select(i % 2 ? "AU" : "GB");
    reader.join();
    catalog.select(before->id);
    REQUIRE(valid.load());
    REQUIRE_FALSE(before->entries.empty());
}
