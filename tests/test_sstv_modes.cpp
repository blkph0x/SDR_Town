#include "SstvModes.h"
#include "SstvVis.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("SSTV Dayton mode table matches VIS and dimensions", "[sstv]")
{
    REQUIRE(sstvModeIdOk("auto"));
    REQUIRE(sstvModeById("robot36")->width == 320);
    REQUIRE(sstvModeById("robot36")->height == 240);
    REQUIRE(sstvModeById("martin1")->height == 256);
    REQUIRE(sstvModeById("pd120")->width == 640);
    REQUIRE(sstvModeById("pd120")->height == 496);
    REQUIRE(sstvModeById("pd290")->width == 800);
    REQUIRE(sstvModeByVis(8)->id == std::string_view("robot36"));
    REQUIRE(sstvModeByVis(44)->id == std::string_view("martin1"));
    REQUIRE(sstvModeByVis(60)->id == std::string_view("scottie1"));
    REQUIRE(sstvModeByVis(95)->id == std::string_view("pd120"));
    REQUIRE(sstvModeIdOk("avt90"));
    REQUIRE(sstvModeByVis(2)->id == std::string_view("robotbw8"));
    REQUIRE(sstvModeByVis(64)->id == std::string_view("avt24"));
    REQUIRE_FALSE(sstvModeIdOk("fax480"));
    REQUIRE(SstvVisDetector::modeName(8) == std::string_view("Robot 36"));
    REQUIRE(SstvVisDetector::modeName(95) == std::string_view("PD 120"));
    REQUIRE(SstvVisDetector::modeName(1) == std::string_view("Unknown"));
}
