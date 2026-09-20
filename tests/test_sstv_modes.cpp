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
    REQUIRE(sstvModeIdOk("fax480"));
    REQUIRE(sstvModeById("fax480")->width == 512);
    REQUIRE(sstvModeById("fax480")->height == 500);
    REQUIRE(sstvModeByVis(36)->id == std::string_view("martin3"));
    REQUIRE(sstvModeByVis(32)->id == std::string_view("martin4"));
    REQUIRE(sstvModeByVis(52)->id == std::string_view("scottie3"));
    REQUIRE(sstvModeByVis(48)->id == std::string_view("scottie4"));
    REQUIRE(sstvModeByVis(16)->id == std::string_view("sc124"));
    REQUIRE(sstvModeIdOk("mp73"));
    REQUIRE(sstvModeIdOk("mr140"));
    REQUIRE(sstvModeIdOk("ml320"));
    REQUIRE(sstvModeIdOk("hamdrm"));
    REQUIRE(sstvModeDimensionsOk("hamdrm", 32, 32));
    REQUIRE(sstvModeByVis(0) == nullptr);
    REQUIRE(SstvVisDetector::modeName(8) == std::string_view("Robot 36"));
    REQUIRE(SstvVisDetector::modeName(95) == std::string_view("PD 120"));
    REQUIRE(SstvVisDetector::modeName(1) == std::string_view("Unknown"));
}
