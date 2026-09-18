#include "SstvStreamWorker.h"
#include "SstvImageFile.h"
#include "miniaudio.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <QApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <future>

namespace {
void requireBackend() {
    const bool present=QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath("sdrtown_sstv.exe"));
#ifdef SDR_TOWN_TEST_SSTV_BACKEND
    REQUIRE(present);
#else
    if(!present) SKIP("Optional SSTV helper not enabled");
#endif
}
SstvInputEvent validBlock() {
    SstvInputEvent block;
    block.count=128; block.sampleRate=48000; block.targetHz=145800000;
    block.sourceId=1; block.epoch=1; block.generation=1;
    return block;
}
}

TEST_CASE("SSTV stream rejects GUI execution and handles empty EOF","[sstv-stream-worker]") {
    const SstvStreamRead empty=[]()->SstvStreamItem{return SstvStreamEnd{};};
    REQUIRE_THROWS(decodeSstvStream(empty,"auto"));
    requireBackend();
    auto task=std::async(std::launch::async,[&]{return decodeSstvStream(empty,"auto");});
    const auto result=task.get();
    REQUIRE(result.images.empty()); REQUIRE(result.inputSamples==0); REQUIRE(result.outputSamples==0);
}

TEST_CASE("SSTV stream idle cancellation reaps worker without requiring input","[sstv-stream-worker]") {
    requireBackend();
    std::atomic<bool> cancel=false,entered=false;
    auto task=std::async(std::launch::async,[&] {
        return decodeSstvStream([&]()->SstvStreamItem{entered=true;return SstvStreamIdle{};},"auto",[&]{return cancel.load();});
    });
    QElapsedTimer clock; clock.start();
    while(!entered && clock.elapsed()<5000) QThread::msleep(1);
    cancel=true;
    REQUIRE_THROWS_WITH(task.get(),Catch::Matchers::ContainsSubstring("cancelled"));
    REQUIRE(entered); REQUIRE(clock.elapsed()<6000);
}

TEST_CASE("SSTV stream rejects gaps and silently changed identities","[sstv-stream-worker]") {
    requireBackend();
    for(int change=0;change<9;++change) {
        CAPTURE(change);
        auto task=std::async(std::launch::async,[change] {
            int calls=0;
            return decodeSstvStream([&]()->SstvStreamItem {
                auto block=validBlock();
                if(calls++==0) return block;
                if(calls>2) return SstvStreamEnd{};
                block.firstSample=128;
                switch(change) {
                case 0: block.gapReasons=static_cast<uint32_t>(SstvInputGap::Position);block.count=0;++block.generation;break;
                case 1: ++block.sourceId; break;
                case 2: ++block.epoch; break;
                case 3: ++block.generation; break;
                case 4: ++block.firstSample; break;
                case 5: block.sampleRate=44100; break;
                case 6: block.targetHz+=12500; break;
                case 7: block.count=SstvInputEvent::maxSamples+1; break;
                case 8: throw std::runtime_error("source fault");
                }
                return block;
            },"auto");
        });
        const auto expected=change==0?"discontinuity":change==7?"block size":change==8?"source fault":"identity/position";
        REQUIRE_THROWS_WITH(task.get(),Catch::Matchers::ContainsSubstring(expected));
    }
}

TEST_CASE("SSTV combined queue converter helper matches independent recorded pixels","[sstv-stream-recording]") {
    const auto input=qEnvironmentVariable("SDR_TOWN_SSTV_STREAM_INPUT");
    if(input.isEmpty()) SKIP("Fixture supplied by scripts/test_sstv_worker.py");
    const auto reference=qEnvironmentVariable("SDR_TOWN_SSTV_STREAM_REFERENCE");
    const auto mode=qEnvironmentVariable("SDR_TOWN_SSTV_STREAM_MODE","auto");
    QTemporaryDir output; REQUIRE(output.isValid());
    const auto expected=decodeSstvImageFile(reference,output.filePath("expected"),mode);
    REQUIRE(expected.at("images").size()==1);
    std::atomic<bool> cancel=false,wrongThread=false;
    std::atomic<int> previews=0;
    auto task=std::async(std::launch::async,[&] {
        ma_decoder decoder{};
        const auto config=ma_decoder_config_init(ma_format_f32,0,0);
        if(ma_decoder_init_file_w(input.toStdWString().c_str(),&config,&decoder)!=MA_SUCCESS)
            throw std::runtime_error("fixture open failed");
        struct Guard {ma_decoder* p;~Guard(){ma_decoder_uninit(p);}} guard{&decoder};
        if(decoder.outputChannels!=1) throw std::runtime_error("fixture is not mono");
        SstvLiveInput queue; queue.start();
        bool ended=false;
        uint64_t first=0;
        return decodeSstvStream([&]()->SstvStreamItem {
            if(const auto pending=queue.pop()) return *pending;
            if(ended) return SstvStreamEnd{};
            FmMultiplexBlock block;
            block.samples.resize(8192); block.sampleRate=decoder.outputSampleRate;
            block.targetHz=145800000; block.epoch=1; block.firstSample=first;block.discontinuity=false;
            ma_uint64 read=0;
            const auto result=ma_decoder_read_pcm_frames(&decoder,block.samples.data(),block.samples.size(),&read);
            if(result!=MA_SUCCESS && result!=MA_AT_END) throw std::runtime_error("fixture read failed");
            if(!read) {ended=true;return SstvStreamEnd{};}
            block.samples.resize(size_t(read)); first+=read;
            if(queue.tryPush(block,7,DemodMode::NFM)!=SstvPushResult::Accepted)
                throw std::runtime_error("fixture queue rejected data");
            return *queue.pop();
        },mode,[&]{return cancel.load();},[&](const QImage&,const QString&,int) {
            ++previews; wrongThread=QThread::currentThread()==qApp->thread();
        });
    });
    QElapsedTimer clock; clock.start();
    while(task.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready && clock.elapsed()<20000)
        QApplication::processEvents();
    if(clock.elapsed()>=20000) cancel=true;
    const auto result=task.get();
    REQUIRE(result.images.size()==1); REQUIRE(previews>1); REQUIRE_FALSE(wrongThread);
    const auto& metadata=expected.at("images")[0];
    const auto name=QString::fromStdString(metadata.at("file").get<std::string>());
    const QImage saved(output.filePath("expected/"+name));
    INFO("stream format="<<int(result.images[0].format())<<" PNG format="<<int(saved.format()));
    REQUIRE(result.images[0]==saved.convertToFormat(QImage::Format_RGB888));
    REQUIRE(result.metadata[0]["rows"]==metadata["rows"]);
    REQUIRE(result.metadata[0]["complete"]==metadata["complete"]);
    REQUIRE(result.sourceId==7); REQUIRE(result.inputSamples>0); REQUIRE(result.outputSamples>0);
}
