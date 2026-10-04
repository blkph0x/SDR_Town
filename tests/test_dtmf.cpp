#include "DtmfDecoder.h"
#include "ControlEventLog.h"
#include "RepeaterMonitor.h"
#include "Receiver.h"

#include "RepeaterControlHooks.h"
#include "Demod.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>
#include <thread>
#include <atomic>
#include <limits>
#include <iostream>
#include <chrono>

TEST_CASE("Repeater controller binds logical identity before active filtering", "[dtmf][ownership]") {
    Receiver primary, sameRadio, otherRadio;
    primary.deviceIndex = sameRadio.deviceIndex = 0;
    otherRadio.deviceIndex = 2;
    CHECK(repeaterControlsReceiver(&primary, &primary));
    CHECK_FALSE(repeaterControlsReceiver(&primary, &sameRadio));
    CHECK_FALSE(repeaterControlsReceiver(&primary, &otherRadio));
    primary.active = false; sameRadio.active = true;
    CHECK_FALSE(repeaterControlsReceiver(&primary, &sameRadio));
    CHECK_FALSE(repeaterControlsReceiver(nullptr, &primary));
    CHECK_FALSE(repeaterControlsReceiver(&primary, nullptr));
    CHECK_FALSE(repeaterControlsReceiver(nullptr, nullptr));
}

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

TEST_CASE("DTMF purity distinguishes a weak pair from broadband energy", "[dtmf][regression]")
{
    auto input = tonePair(697, 1209, 8000, .20, .02);
    uint32_t seed = 42;
    for (auto& sample : input) {
        seed = 1664525u * seed + 1013904223u;
        sample += float(int32_t(seed)) / 2147483648.f * .3f;
    }
    DtmfDecoder decoder;
    const auto s = feed(decoder, input);
    REQUIRE(s.purity < .3);
    REQUIRE(s.confirmedDigits == 0);
}

TEST_CASE("DTMF conservative detector rejects a 23 ms transient", "[dtmf][regression]")
{
    DtmfDecoder decoder;
    const auto s = feed(decoder, concat(tonePair(697, 1209, 8000, .023), silence(8000, .4)));
    REQUIRE(s.confirmedDigits == 0);
}

TEST_CASE("DTMF fast 20 ms bursts cover all keys rates polarity and start phases", "[dtmf][burst]")
{
    const double rows[]{697, 770, 852, 941}, cols[]{1209, 1336, 1477, 1633};
    const std::string keys = "123A456B789C*0#D";
    for (unsigned rate : {8000u, 11025u, 44100u, 48000u, 96000u}) {
        for (bool inverted : {false, true}) for (unsigned offset : {0u, 7u, 19u}) {
            INFO("rate=" << rate << " inverted=" << inverted << " offset=" << offset);
            DtmfOptions options; options.fast = true; options.inverted = inverted;
            DtmfDecoder decoder(options);
            std::vector<float> input(offset, 0);
            for (size_t i = 0; i < keys.size(); ++i) {
                const double r = rows[i / 4], c = cols[i % 4];
                input = concat(std::move(input), tonePair(inverted ? 3300-r : r, inverted ? 3300-c : c,
                    rate, .020, -.15));
                input = concat(std::move(input), silence(rate, .015));
            }
            feed(decoder, input, rate); decoder.finish();
            const auto s = decoder.snapshot();
            REQUIRE(s.lastSequence == keys);
            REQUIRE(s.confirmedDigits == 16);
            REQUIRE(s.samples == input.size());
            REQUIRE(s.history.size() == 16);
        }
    }
}

TEST_CASE("DTMF conservative 40 ms keys tolerate frequency offsets", "[dtmf]")
{
    const double rows[]{697,770,852,941}, cols[]{1209,1336,1477,1633};
    const std::string keys="123A456B789C*0#D";
    for (size_t key=0;key<16;++key) for (double error : {-.015, 0.0, .015}) for (unsigned offset : {0u, 7u, 19u}) {
        INFO("error=" << error << " offset=" << offset);
        DtmfDecoder decoder;
        auto input = concat(std::vector<float>(offset, 0), tonePair(rows[key/4]*(1+error), cols[key%4]*(1+error), 8000, .040));
        feed(decoder, concat(input, silence(8000, .4)));
        REQUIRE(decoder.snapshot().lastSequence == std::string(1,keys[key]));
    }
}

TEST_CASE("DTMF repeated fast keys are chunk invariant and sample timestamped", "[dtmf]")
{
    auto input = concat(tonePair(697, 1477, 48000, .020), silence(48000, .015));
    input = concat(input, tonePair(697, 1477, 48000, .020));
    std::vector<DtmfDetection> reference;
    for (size_t chunk : {size_t(1), size_t(137), size_t(4096)}) {
        DtmfOptions o; o.fast = true;
        DtmfDecoder decoder(o);
        for (size_t i = 0; i < input.size(); i += chunk)
            REQUIRE(decoder.process(std::span(input).subspan(i, std::min(chunk, input.size()-i)), 48000, 100e6, 9, i+999, i==0));
        decoder.finish();
        const auto s = decoder.snapshot();
        REQUIRE(s.lastSequence == "33");
        REQUIRE(s.history.size() == 2);
        if (reference.empty()) reference = s.history;
        for (size_t i = 0; i < 2; ++i) {
            REQUIRE(s.history[i].epoch == 9);
            REQUIRE(s.history[i].firstSample >= 999);
            REQUIRE(s.history[i].firstSample == reference[i].firstSample);
            REQUIRE(s.history[i].confirmedSample == reference[i].confirmedSample);
        }
    }
}

TEST_CASE("DTMF transformed profiles reject invalid settings and reset across changes", "[dtmf]")
{
    DtmfOptions o; o.fast = true; o.pitchScale = 1.12; o.shiftHz = -120;
    DtmfDecoder decoder(o);
    feed(decoder, tonePair(770*1.12-120, 1336*1.12-120, 8000, .04));
    decoder.finish(); REQUIRE(decoder.snapshot().lastSequence == "5");
    o.inversionHz = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_THROWS_AS(decoder.setOptions(o), std::invalid_argument);
    o.inversionHz = 6000; o.inverted = true;
    decoder.setOptions(o);
    REQUIRE_FALSE(decoder.process(silence(8000,.01), 8000, 0, 1, 0, true));
}

TEST_CASE("DTMF rejects single tones competing pairs and very short pulses", "[dtmf]")
{
    for (bool fast : {false, true}) for (int kind = 0; kind < 5; ++kind) {
        INFO("fast=" << fast << " negative=" << kind);
        DtmfOptions o; o.fast = fast;
        DtmfDecoder decoder(o);
        auto input = tonePair(697, kind == 0 ? 697 : 1209, 8000, kind == 1 ? .008 : .20);
        if (kind == 2) {
            auto competing = tonePair(941, 1633, 8000, .20);
            for (size_t i=0; i<input.size(); ++i) input[i] += competing[i];
        }
        if (kind == 3) input = tonePair(697*1.07, 1209*1.07, 8000, .20);
        if (kind == 4) input = tonePair(350, 440, 8000, .20);
        feed(decoder, concat(input, silence(8000,.4)));
        REQUIRE(decoder.snapshot().confirmedDigits == 0);
    }
}

TEST_CASE("DTMF event drain is concurrent and retains source identity across reset", "[dtmf]")
{
    DtmfDecoder decoder;
    std::atomic<bool> done{false};
    std::vector<DtmfDecoder::PendingEvent> events;
    std::thread consumer([&] {
        while (!done) {
            auto e = decoder.takePendingEvents();
            events.insert(events.end(), e.begin(), e.end());
            (void)decoder.snapshot(); std::this_thread::yield();
        }
    });
    auto input = tonePair(770, 1336, 8000, .05);
    bool ok = true;
    for (unsigned i=0; i<20; ++i) ok &= decoder.process(input,8000,100e6+i*1e6,i,0,true);
    decoder.finish(); done = true; consumer.join();
    const auto remaining = decoder.takePendingEvents(); events.insert(events.end(),remaining.begin(),remaining.end());
    REQUIRE(ok);
    REQUIRE(events.size() == 40); // Below the bounded queue even if the consumer is descheduled.
    for (size_t i=0; i<20; ++i) {
        REQUIRE(events[i*2].targetHz == 100e6+i*1e6);
        REQUIRE(events[i*2+1].targetHz == 100e6+i*1e6);
    }
}

TEST_CASE("DTMF chronological NFM dual-watch preserves independent short sequences", "[dtmf][rf]")
{
    constexpr double rate=2400000, center=476.8375e6, seconds=.30;
    constexpr size_t chunk=24000;
    const double targets[]{center-375000,center+375000};
    Demodulator demods[2];
    DtmfOptions options;options.fast=true;
    DtmfDecoder decoders[2]{DtmfDecoder(options),DtmfDecoder(options)};
    std::vector<std::complex<float>> iq(chunk);
    double phase[2]{};
    const auto start=std::chrono::steady_clock::now();
    for (size_t offset=0;offset<size_t(rate*seconds);offset+=chunk) {
        for (size_t i=0;i<chunk;++i) {
            const double t=(offset+i)/rate;
            const bool active=(t>=.050 && t<.070)||(t>=.085 && t<.105);
            iq[i]={};
            for (size_t leg=0;leg<2;++leg) {
                const double row=leg?770:697, col=leg?1336:1209;
                const double audio=active?.5*(std::sin(2*std::numbers::pi*row*t)+std::sin(2*std::numbers::pi*col*t)):0;
                phase[leg]=std::remainder(phase[leg]+2*std::numbers::pi*((targets[leg]-center)+1800*audio)/rate,2*std::numbers::pi);
                iq[i]+=.4f*std::polar(1.f,float(phase[leg]));
            }
        }
        for (size_t leg=0;leg<2;++leg) {
            FmMultiplexBlock mpx;double rms=0;
            (void)demods[leg].demodulateToAudio(iq,rate,center,targets[leg],DemodMode::NFM,rms,
                3000,-200,1,75,.96,12500,0,48000,std::numeric_limits<double>::quiet_NaN(),false,&mpx,targets[leg]);
            REQUIRE(decoders[leg].process(mpx.samples,mpx.sampleRate,targets[leg],mpx.epoch,mpx.firstSample,mpx.discontinuity));
        }
    }
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"DTMF dual 2.4 Msps RF: "<<seconds<<" seconds input, "<<elapsed<<" seconds synthesis+DSP wall time\n";
    for (size_t leg=0;leg<2;++leg) {
        decoders[leg].finish();const auto s=decoders[leg].snapshot();
        INFO("leg="<<leg<<" sequence="<<s.lastSequence);
        REQUIRE(s.lastSequence==(leg?"55":"11"));REQUIRE(s.resets==1);
        ControlEventLog log;
        drainDtmfEvents(decoders[leg],log,ControlEvent::Channel::Tuned,999e6);
        std::vector<ControlEvent> events;log.drainSince(0,events);
        REQUIRE(events.size()==1);REQUIRE(events[0].freqHz==targets[leg]);
    }
}

TEST_CASE("DTMF accepts weak DC-biased tones and rejects excessive twist and voiced harmonics", "[dtmf]")
{
    for (bool fast:{false,true}) for (int scenario=0;scenario<5;++scenario) {
        INFO("fast="<<fast<<" scenario="<<scenario);
        DtmfOptions options;options.fast=fast;DtmfDecoder decoder(options);
        auto input=tonePair(697,1209,8000,.08,.003);
        for (size_t i=0;i<input.size();++i) {
            const double t=i/8000.0;
            if (scenario==0) input[i]+=.2f;
            if (scenario==1) input[i]=float(.01*std::sin(2*std::numbers::pi*697*t)+.03*std::sin(2*std::numbers::pi*1209*t));
            if (scenario==2) input[i]=float(.03*std::sin(2*std::numbers::pi*697*t)+.006*std::sin(2*std::numbers::pi*1209*t));
            if (scenario==3) {
                input[i]=0;
                for (int harmonic=1;harmonic<=15;++harmonic)
                    input[i]+=float(.1/harmonic*std::sin(2*std::numbers::pi*harmonic*100*t));
            }
            if (scenario==4) input[i]=std::clamp(input[i]*100,-.3f,.3f);
        }
        feed(decoder,concat(input,silence(8000,.4)));
        REQUIRE(decoder.snapshot().confirmedDigits==((scenario==0||scenario==4)?1:0));
    }
}

TEST_CASE("DTMF bounds diagnostics and reports overflow explicitly", "[dtmf]")
{
    DtmfOptions options;options.fast=true;DtmfDecoder decoder(options);
    std::vector<float> input;
    for (int i=0;i<80;++i) {
        input=concat(std::move(input),tonePair(697,1209,8000,.020));
        input=concat(std::move(input),silence(8000,.015));
    }
    feed(decoder,input);decoder.finish();const auto s=decoder.snapshot();
    REQUIRE(s.confirmedDigits==80);REQUIRE(s.history.size()==64);
    REQUIRE(s.lastSequence.size()==32);REQUIRE(s.truncatedDigits==48);
    REQUIRE(s.droppedEvents==17);REQUIRE(decoder.takePendingEvents().size()==64);
}

TEST_CASE("DTMF tone phases do not change the fast decoded key", "[dtmf]")
{
    for (int phase=0;phase<12;++phase) {
        DtmfOptions options;options.fast=true;DtmfDecoder decoder(options);
        auto input=tonePair(852,1477,8000,.020);
        for (size_t i=0;i<input.size();++i)
            input[i]=float(.2*(std::sin(2*std::numbers::pi*852*i/8000+phase*.47)+
                std::sin(2*std::numbers::pi*1477*i/8000-phase*.31)));
        feed(decoder,input);decoder.finish();
        REQUIRE(decoder.snapshot().lastSequence=="9");
    }
}
