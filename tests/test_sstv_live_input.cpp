#include <catch2/catch_test_macros.hpp>
#include "SstvLiveInput.h"
#include "SstvReceiverFeed.h"
#include "SstvRfMode.h"
#include <limits>
#include <thread>

namespace {
FmMultiplexBlock block(size_t count=128,uint64_t first=0) {
    FmMultiplexBlock b;
    b.samples.resize(count);
    for(size_t i=0;i<count;++i) b.samples[i]=float(first+i);
    b.sampleRate=48000; b.targetHz=145800000;
    b.epoch=1; b.firstSample=first; b.discontinuity=false;
    return b;
}
bool has(const SstvInputEvent& e,SstvInputGap gap) {
    return (e.gapReasons&static_cast<uint32_t>(gap))!=0;
}
void start(SstvLiveInput& input) {
    input.start();
    const auto event=input.pop();
    REQUIRE(event); REQUIRE(has(*event,SstvInputGap::Start)); REQUIRE(event->count==0);
}
}

TEST_CASE("SSTV ingress preserves samples and positions across chunks","[sstv][live-input]") {
    SstvLiveInput input;
    REQUIRE(input.tryPush(block(),1,DemodMode::NFM)==SstvPushResult::Inactive);
    start(input);
    uint64_t first=0;
    for(const size_t count:{size_t{1},size_t{137},size_t{8192}}) {
        auto b=block(count,first);
        b.sampleRate=2048000.0/43.0;
        REQUIRE(input.tryPush(b,7,DemodMode::NFM)==SstvPushResult::Accepted);
        const auto e=input.pop(); REQUIRE(e); REQUIRE(e->gapReasons==0);
        REQUIRE(e->count==count); REQUIRE(e->firstSample==first);
        REQUIRE(e->sampleRate==b.sampleRate); REQUIRE(e->sourceId==7);
        for(size_t i=0;i<count;++i) REQUIRE(e->samples[i]==b.samples[i]);
        first+=count;
    }
    REQUIRE_FALSE(input.pop());
    const auto s=input.stats(); REQUIRE(s.acceptedSamples==first);
    REQUIRE(s.consumedSamples==first); REQUIRE(s.discardedSamples==0);
}

TEST_CASE("SSTV ingress accepts FM and selected SSB audio sources","[sstv][live-input]") {
    for(const auto mode:{DemodMode::NFM,DemodMode::USB,DemodMode::LSB}) {
        SstvLiveInput input; start(input);
        REQUIRE(input.tryPush(block(),1,mode)==SstvPushResult::Accepted);
        const auto event=input.pop();
        REQUIRE(event); REQUIRE(event->gapReasons==0); REQUIRE(event->count==128);
    }

    SstvLiveInput unrelated; start(unrelated);
    REQUIRE(unrelated.tryPush(block(),1,DemodMode::WFM)==SstvPushResult::Invalid);
    const auto gap=unrelated.pop();
    REQUIRE(gap); REQUIRE(has(*gap,SstvInputGap::Invalid));
}

TEST_CASE("SSTV ingress source changes discard stale queued samples","[sstv][live-input]") {
    for(int change=0;change<7;++change) {
        SstvLiveInput input; start(input);
        REQUIRE(input.tryPush(block(),1,DemodMode::NFM)==SstvPushResult::Accepted);
        auto b=block(128,128); uint64_t source=1;
        switch(change) {
        case 0: source=2; break;
        case 1: ++b.epoch; break;
        case 2: b.sampleRate=44100; break;
        case 3: b.targetHz+=12500; break;
        case 4: b.discontinuity=true; break;
        case 5: ++b.firstSample; break;
        case 6: b.firstSample=0; break;
        }
        REQUIRE(input.tryPush(b,source,DemodMode::NFM)==SstvPushResult::Accepted);
        const auto gap=input.pop(); REQUIRE(gap);
        REQUIRE(has(*gap,change<5?SstvInputGap::Source:SstvInputGap::Position));
        const auto e=input.pop(); REQUIRE(e); REQUIRE(e->generation==gap->generation);
        REQUIRE(e->firstSample==b.firstSample); REQUIRE_FALSE(input.pop());
        REQUIRE(input.stats().discardedSamples==128);
    }
}

TEST_CASE("SSTV ingress rejects malformed data with an explicit gap","[sstv][live-input]") {
    for(int change=0;change<10;++change) {
        SstvLiveInput input; start(input);
        REQUIRE(input.tryPush(block(),1,DemodMode::NFM)==SstvPushResult::Accepted);
        auto b=block(128,128); auto mode=DemodMode::NFM;
        switch(change) {
        case 0: b.samples.clear(); break;
        case 1: b.samples.resize(8193); break;
        case 2: b.samples[4]=std::numeric_limits<float>::quiet_NaN(); break;
        case 3: b.samples[4]=std::numeric_limits<float>::infinity(); break;
        case 4: b.sampleRate=7999; break;
        case 5: b.sampleRate=96001; break;
        case 6: b.sampleRate=std::numeric_limits<double>::quiet_NaN(); break;
        case 7: b.targetHz=0; break;
        case 8: b.firstSample=std::numeric_limits<uint64_t>::max(); break;
        case 9: mode=DemodMode::WFM; break;
        }
        REQUIRE(input.tryPush(b,1,mode)==SstvPushResult::Invalid);
        const auto e=input.pop(); REQUIRE(e); REQUIRE(has(*e,SstvInputGap::Invalid));
        REQUIRE_FALSE(input.pop()); REQUIRE(input.stats().invalidBlocks==1);
        REQUIRE(input.stats().discardedSamples==128+b.samples.size());
    }
}

TEST_CASE("SSTV ingress overflow has bounded storage and does not splice","[sstv][live-input]") {
    SECTION("Slot capacity") {
        SstvLiveInput input; start(input);
        for(uint64_t i=0;i<17;++i)
            REQUIRE(input.tryPush(block(128,i*128),1,DemodMode::NFM)==SstvPushResult::Accepted);
        const auto gap=input.pop(); REQUIRE(gap); REQUIRE(has(*gap,SstvInputGap::Overflow));
        const auto e=input.pop(); REQUIRE(e); REQUIRE(e->firstSample==2048);
        REQUIRE_FALSE(input.pop()); REQUIRE(input.stats().discardedSamples==2048);
    }
    SECTION("Two second sample budget") {
        SstvLiveInput input; start(input);
        for(uint64_t i=0;i<3;++i) {
            auto b=block(8000,i*8000); b.sampleRate=8000;
            REQUIRE(input.tryPush(b,1,DemodMode::NFM)==SstvPushResult::Accepted);
        }
        const auto gap=input.pop(); REQUIRE(gap); REQUIRE(has(*gap,SstvInputGap::Overflow));
        REQUIRE(input.stats().queuedSamples==8000); REQUIRE(input.stats().discardedSamples==16000);
        const auto e=input.pop(); REQUIRE(e); REQUIRE(e->firstSample==16000);
    }
}

TEST_CASE("SSTV ingress stop and restart cannot replay buffered audio","[sstv][live-input]") {
    SstvLiveInput input; start(input);
    REQUIRE(input.tryPush(block(),1,DemodMode::NFM)==SstvPushResult::Accepted);
    const auto generation=input.stats().generation;
    input.stop();
    REQUIRE_FALSE(input.stats().active);
    REQUIRE(input.tryPush(block(),1,DemodMode::NFM)==SstvPushResult::Inactive);
    const auto gap=input.pop(); REQUIRE(gap); REQUIRE(has(*gap,SstvInputGap::Stop));
    REQUIRE_FALSE(input.pop()); start(input);
    REQUIRE(input.stats().generation>generation);
    REQUIRE(input.tryPush(block(),2,DemodMode::NFM)==SstvPushResult::Accepted);
    const auto e=input.pop(); REQUIRE(e); REQUIRE(e->sourceId==2);
    REQUIRE(input.stats().discardedSamples==128);
}

TEST_CASE("SSTV concurrent ingress never conceals a generation boundary","[sstv][live-input]") {
    SstvLiveInput input; start(input);
    std::atomic<bool> done{false};
    bool ordered=true;
    uint64_t generation=input.stats().generation,next=0;
    bool havePosition=false;
    std::thread producer([&] {
        auto b=block(128);
        for(uint64_t i=0;i<20000;++i) {
            b.firstSample=i*128;
            for(size_t j=0;j<b.samples.size();++j) b.samples[j]=float(b.firstSample+j);
            input.tryPush(b,1,DemodMode::NFM);
        }
        done.store(true);
    });
    // No Catch assertions until the producer is joined (including failure paths).
    for(;;) {
        const bool finished=done.load();
        const auto e=input.pop();
        if(!e) {
            if(finished) break;
            std::this_thread::yield(); continue;
        }
        if(e->gapReasons) {
            ordered=ordered && e->count==0 && e->generation>generation;
            generation=e->generation; havePosition=false;
        } else {
            ordered=ordered && e->generation==generation && (!havePosition || e->firstSample==next);
            for(size_t j=0;j<e->count;++j) ordered=ordered && e->samples[j]==float(e->firstSample+j);
            next=e->firstSample+e->count; havePosition=true;
        }
    }
    producer.join();
    REQUIRE(ordered);
    const auto s=input.stats();
    REQUIRE(s.queuedBlocks==0); REQUIRE(s.queuedSamples==0);
    REQUIRE(s.consumedSamples+s.discardedSamples==20000*128);
    REQUIRE(s.acceptedSamples+s.contendedBlocks*128==20000*128);
}

TEST_CASE("SSTV receiver attachment owns lifecycle and rejects overlapping sessions","[sstv][live-input]") {
    SstvReceiverFeed feed;
    feed.detach(nullptr); feed.publish(block(),1);
    auto first=feed.attach();
    REQUIRE_THROWS(feed.attach());
    REQUIRE(first->pop()->gapReasons!=0);
    feed.publish(block(),1);
    REQUIRE(first->pop()->count==128);
    feed.discontinuity();
    REQUIRE(has(*first->pop(),SstvInputGap::Source));
    feed.detach(first); REQUIRE_FALSE(first->stats().active);
    auto second=feed.attach(); feed.detach(first);
    REQUIRE(second->stats().active);
    REQUIRE(second->pop()->gapReasons!=0);
    feed.publish(block(),2); REQUIRE(second->pop()->sourceId==2);
    feed.publish(block(128,128),2); feed.finish(second);
    REQUIRE_FALSE(second->stats().active);
    feed.publish(block(128,256),2);
    const auto tail=second->pop(); REQUIRE(tail); REQUIRE(tail->firstSample==128);
    REQUIRE_FALSE(second->pop());
    feed.detach(second);
}

TEST_CASE("SSTV receiver detach quiesces an active producer","[sstv][live-input]") {
    SstvReceiverFeed feed; std::atomic<bool> done=false;
    std::thread producer([&] {
        auto data=block();
        while(!done.load()) {feed.publish(data,1); data.firstSample+=data.samples.size();}
    });
    bool stopped=true;
    for(int i=0;i<100;++i) {
        auto session=feed.attach();
        std::this_thread::yield(); feed.detach(session);
        stopped=stopped && !session->stats().active;
    }
    done=true; producer.join(); REQUIRE(stopped);
}


TEST_CASE("SSTV RF auto selection detects sideband energy and safe fallbacks","[sstv][rf-mode]") {
    constexpr size_t bins=4096;
    constexpr double rate=48000.0;
    constexpr double center=14.230e6;
    const double binHz=rate/double(bins);
    std::vector<float> usb(bins,-120.0f),lsb(bins,-120.0f),symmetric(bins,-120.0f);
    auto paint=[&](std::vector<float>& p,double offset,float level) {
        const double start=center-rate*0.5;
        const size_t index=std::min(bins-1,size_t((center+offset-start)/binHz));
        for(int d=-4;d<=4;++d) {
            const long at=long(index)+d;
            if(at>=0 && at<long(bins)) p[size_t(at)]=level;
        }
    };
    paint(usb,1700.0f,-45.0f); paint(usb,-1700.0f,-80.0f);
    paint(lsb,-1700.0f,-45.0f); paint(lsb,1700.0f,-80.0f);
    paint(symmetric,-1700.0f,-48.0f); paint(symmetric,1700.0f,-48.0f);

    CHECK(SstvRfMode::select("auto",DemodMode::NFM,center,usb,center,rate).mode==DemodMode::USB);
    CHECK(SstvRfMode::select("auto",DemodMode::NFM,center,lsb,center,rate).mode==DemodMode::LSB);
    CHECK(SstvRfMode::select("auto",DemodMode::AUTO,145.8e6,symmetric,145.8e6,rate).mode==DemodMode::NFM);
    CHECK(SstvRfMode::select("auto",DemodMode::AUTO,7.171e6).mode==DemodMode::LSB);
    CHECK(SstvRfMode::select("auto",DemodMode::AUTO,14.230e6).mode==DemodMode::USB);
    CHECK(SstvRfMode::select("auto",DemodMode::NFM,14.230e6).mode==DemodMode::USB);
    CHECK(SstvRfMode::select("nfm",DemodMode::USB,14.230e6).mode==DemodMode::NFM);
}
