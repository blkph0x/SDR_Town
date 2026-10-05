#include <catch2/catch_all.hpp>
#include "P25PipelineTrace.h"
#include <thread>
#include <limits>

TEST_CASE("P25 trace is bounded ordered and explicit about loss", "[p25][trace]") {
    P25PipelineTrace<2> trace;
    P25PipelineEvent event;
    REQUIRE_FALSE(trace.push(event));
    trace.start();
    REQUIRE(trace.push(event)); REQUIRE(trace.push(event));
    REQUIRE_FALSE(trace.push(event)); REQUIRE(trace.dropped() == 1);
    auto rows = trace.drain();
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].traceSequence == 1); REQUIRE(rows[1].traceSequence == 2);
    REQUIRE(rows[1].monotonicUs >= rows[0].monotonicUs);
    REQUIRE(trace.push(event)); trace.stop();
    REQUIRE_FALSE(trace.push(event)); REQUIRE(trace.drain().size() == 1);
    trace.start(); REQUIRE(trace.dropped() == 0); REQUIRE(trace.drain().empty());
    P25PipelineEvent::text(event.stage, "a deliberately longer than thirty two character stage name");
    REQUIRE(std::strlen(event.stage) == 31);
}

TEST_CASE("P25 trace accounts for every concurrent producer attempt", "[p25][trace]") {
    P25PipelineTrace<64> trace;
    trace.start();
    auto produce = [&] { for (int i = 0; i < 1000; ++i) trace.push({}); };
    std::thread a(produce), b(produce), c(produce);
    a.join(); b.join(); c.join(); trace.stop();
    auto rows = trace.drain();
    REQUIRE(rows.size() + trace.dropped() == 3000);
    for (size_t i = 1; i < rows.size(); ++i)
        REQUIRE(rows[i].traceSequence == rows[i - 1].traceSequence + 1);
}

TEST_CASE("P25 CC results require the same acquisition and monitor identity", "[p25][cc-context]") {
    P25ControlContext source{0, 3, 4, 420350000, 2048000, 420350000};
    REQUIRE(source.matches(source));
    auto current = source;
    SECTION("out and back retune") { ++current.streamEpoch; }
    SECTION("new monitor at same frequency") { ++current.resetGeneration; }
    SECTION("other device") { ++current.device; }
    SECTION("changed center") { current.centerHz += 1; }
    SECTION("changed rate") { current.sampleRate += 1; }
    SECTION("changed target") { current.targetHz += 1; }
    SECTION("nonfinite metadata") { current.sampleRate = std::numeric_limits<double>::quiet_NaN(); }
    REQUIRE_FALSE(source.matches(current));
}

TEST_CASE("P25 CC cannot validate uninitialized tuning metadata", "[p25][cc-context]") {
    P25ControlContext source{0, 3, 4, 0, 2048000, 420350000};
    REQUIRE_FALSE(source.matches(source));
}
