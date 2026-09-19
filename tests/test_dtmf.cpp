#include "DtmfDecoder.h"
#include "ControlEventLog.h"
#include "RepeaterMonitor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

namespace {
std::vector<float> tonePair(double rowHz, double colHz, unsigned rate, double seconds,
                            double gain = 0.2)
{
    const size_t n = static_cast<size_t>(std::llround(rate * seconds));
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / rate;
        out[i] = static_cast<float>(gain * (std::sin(2 * std::numbers::pi * rowHz * t) +
                                            std::sin(2 * std::numbers::pi * colHz * t)));
    }
    return out;
}

std::vector<float> silence(unsigned rate, double seconds)
{
    return std::vector<float>(static_cast<size_t>(std::llround(rate * seconds)), 0.f);
}

std::vector<float> concat(std::vector<float> a, const std::vector<float>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

DtmfSnapshot feed(DtmfDecoder& decoder, const std::vector<float>& input, unsigned rate = 8000)
{
    for (size_t i = 0; i < input.size();) {
        const size_t n = std::min<size_t>(512, input.size() - i);
        REQUIRE(decoder.process(std::span(input.data() + i, n), rate, 477.2125e6, 1, i, i == 0));
        i += n;
    }
    return decoder.snapshot();
}
}

TEST_CASE("DTMF decodes standard keypad digits robustly", "[dtmf]")
{
    struct Case { char digit; double row; double col; };
    const Case cases[]{
        {'1', 697, 1209}, {'2', 697, 1336}, {'3', 697, 1477}, {'A', 697, 1633},
        {'4', 770, 1209}, {'5', 770, 1336}, {'0', 941, 1336}, {'*', 941, 1209},
        {'#', 941, 1477}, {'D', 941, 1633}};
    for (const auto& c : cases) {
        INFO("digit=" << c.digit);
        DtmfDecoder decoder;
        auto snap = feed(decoder, concat(tonePair(c.row, c.col, 8000, 0.12), silence(8000, 0.40)));
        REQUIRE(snap.confirmedDigits >= 1);
        REQUIRE(snap.lastSequence == std::string(1, c.digit));
    }
}

TEST_CASE("DTMF builds multi-digit sequences and rejects noise", "[dtmf]")
{
    DtmfDecoder decoder;
    auto seq = concat(tonePair(697, 1209, 8000, 0.1), silence(8000, 0.05)); // 1
    seq = concat(std::move(seq), concat(tonePair(697, 1336, 8000, 0.1), silence(8000, 0.05))); // 2
    seq = concat(std::move(seq), concat(tonePair(697, 1477, 8000, 0.1), silence(8000, 0.40))); // 3
    auto snap = feed(decoder, seq);
    REQUIRE(snap.lastSequence == "123");
    REQUIRE(snap.confirmedDigits == 3);

    DtmfDecoder noisy;
    std::vector<float> block(16000);
    uint32_t random = 0xC0FFEE;
    for (auto& s : block) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        s = float(int32_t(random)) / 2147483648.f * 0.3f;
    }
    feed(noisy, block);
    REQUIRE(noisy.snapshot().confirmedDigits == 0);
}

TEST_CASE("DTMF long keypress confirms once and repeats need a gap", "[dtmf]")
{
    // One sustained tone must not double-fire across overlapping Goertzel hops.
    DtmfDecoder held;
    auto snap = feed(held, concat(tonePair(697, 1209, 8000, 0.35), silence(8000, 0.40)));
    REQUIRE(snap.confirmedDigits == 1);
    REQUIRE(snap.lastSequence == "1");

    // Repeated same digit (e.g. 1337) still works when the operator leaves a gap.
    DtmfDecoder repeat;
    auto seq = concat(tonePair(697, 1209, 8000, 0.12), silence(8000, 0.06)); // 1
    seq = concat(std::move(seq), concat(tonePair(697, 1477, 8000, 0.12), silence(8000, 0.06))); // 3
    seq = concat(std::move(seq), concat(tonePair(697, 1477, 8000, 0.12), silence(8000, 0.06))); // 3
    seq = concat(std::move(seq), concat(tonePair(852, 1209, 8000, 0.12), silence(8000, 0.40))); // 7
    snap = feed(repeat, seq);
    REQUIRE(snap.lastSequence == "1337");
    REQUIRE(snap.confirmedDigits == 4);
}

TEST_CASE("DTMF clears on retune gap and invalid rates", "[dtmf]")
{
    DtmfDecoder decoder;
    auto ok = tonePair(770, 1336, 8000, 0.15);
    REQUIRE(decoder.process(ok, 8000, 100e6, 1, 0, true));
    REQUIRE(decoder.snapshot().digit == '5');
    REQUIRE(decoder.process(std::span(ok.data(), 200), 8000, 100e6, 1, ok.size() + 10, false));
    REQUIRE(decoder.snapshot().resets >= 2);
    std::vector<float> silenceBuf(8000, 0.f);
    REQUIRE_FALSE(decoder.process(silenceBuf, 4000, 100e6, 1, 0, true));
}

TEST_CASE("Repeater dual-watch planner accepts AU 750 kHz pairs at RTL rates", "[dtmf][repeater]")
{
    const auto plan = planRepeaterDualWatch(476.4625e6, 477.2125e6, 2.4e6, 12.5e3);
    REQUIRE(plan.feasible);
    REQUIRE(plan.centerHz == Catch::Approx(476.8375e6).margin(1.0));
    REQUIRE_FALSE(planRepeaterDualWatch(476.4625e6, 477.2125e6, 240e3, 12.5e3).feasible);
    REQUIRE(frequencyInPassband(476.4625e6, plan.centerHz, 2.4e6, 12.5e3));
    REQUIRE(frequencyInPassband(477.2125e6, plan.centerHz, 2.4e6, 12.5e3));
}

TEST_CASE("Control event log rings and drains by index", "[dtmf][repeater]")
{
    ControlEventLog log(4);
    for (int i = 0; i < 6; ++i) {
        ControlEvent e;
        e.ms = i;
        e.kind = ControlEvent::Kind::DtmfDigit;
        e.detail = std::to_string(i);
        log.push(std::move(e));
    }
    REQUIRE(log.size() == 4);
    std::vector<ControlEvent> drained;
    const auto hi = log.drainSince(0, drained);
    REQUIRE(hi == 6);
    REQUIRE(drained.size() == 4);
    REQUIRE(drained.front().detail == "2");
    REQUIRE(std::string(ControlEventLog::kindName(ControlEvent::Kind::CarrierOpen)) == "carrier_open");
}
