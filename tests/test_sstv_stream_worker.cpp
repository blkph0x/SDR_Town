#include "SstvStreamWorker.h"
#include "SstvImageFile.h"
#include "SstvModes.h"
#include "SstvLiveSession.h"
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

TEST_CASE("Every advertised analog SSTV mode can open a live worker", "[sstv-stream-worker]") {
    requireBackend();
    for (const auto& spec : kSstvModes) {
        const QString mode = QString::fromUtf8(spec.id);
        if (!sstvStreamingModeOk(spec.id)) continue;
        CAPTURE(spec.id);
        auto task = std::async(std::launch::async, [mode] {
            return decodeSstvStream([]()->SstvStreamItem { return SstvStreamEnd{}; }, mode);
        });
        REQUIRE(task.get().inputSamples == 0);
    }
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
    REQUIRE_FALSE(expected.at("images").empty());
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
    REQUIRE(result.images.size()==expected.at("images").size()); REQUIRE(previews>1); REQUIRE_FALSE(wrongThread);
    for (size_t index = 0; index < result.images.size(); ++index) {
    const auto& metadata=expected.at("images")[index];
    const auto name=QString::fromStdString(metadata.at("file").get<std::string>());
    const QImage saved(output.filePath("expected/"+name));
    INFO("image=" << index);
    REQUIRE(result.images[index]==saved.convertToFormat(QImage::Format_RGB888));
    REQUIRE(result.metadata[index]["rows"]==metadata["rows"]);
    REQUIRE(result.metadata[index]["complete"]==metadata["complete"]);
    }
    REQUIRE(result.sourceId==7); REQUIRE(result.inputSamples>0); REQUIRE(result.outputSamples>0);
}

TEST_CASE("SSTV archive report preserves rolling image names", "[sstv-stream-worker]") {
    QTemporaryDir directory;REQUIRE(directory.isValid());
    SstvStreamResult result;result.archived=true;result.savedImages=100;
    result.metadata=nlohmann::json::array({{{"file","image-99.png"},{"complete",true}}});
    const auto report=saveSstvLiveResult(result,directory.path());
    CHECK(report.at("images")[0].at("file")=="image-99.png");CHECK(report.at("savedImages")==100);
}

TEST_CASE("SSTV continuous archive saves repeated images before EOF and survives cancellation", "[.sstv-archive-recording]") {
    requireBackend();
    const auto input=qEnvironmentVariable("SDR_TOWN_SSTV_STREAM_INPUT");
    REQUIRE_FALSE(input.isEmpty());
    ma_decoder decoder{};const auto config=ma_decoder_config_init(ma_format_f32,1,48000);
    REQUIRE(ma_decoder_init_file_w(input.toStdWString().c_str(),&config,&decoder)==MA_SUCCESS);
    struct Guard{ma_decoder* p;~Guard(){ma_decoder_uninit(p);}} guard{&decoder};
    ma_uint64 length=0;REQUIRE(ma_decoder_get_length_in_pcm_frames(&decoder,&length)==MA_SUCCESS);
    REQUIRE(length>0);REQUIRE(length<48000*180);
    std::vector<float> samples(size_t(length),0.f);ma_uint64 read=0;
    const auto status=ma_decoder_read_pcm_frames(&decoder,samples.data(),length,&read);
    REQUIRE((status==MA_SUCCESS || status==MA_AT_END));REQUIRE(read==length);
    for(bool cancelAfterSave:{false,true}) {
        CAPTURE(cancelAfterSave);
        QTemporaryDir directory;REQUIRE(directory.isValid());const auto path=directory.filePath("archive");
        std::atomic<bool> stop=false;bool observed=false;
        auto task=std::async(std::launch::async,[&] {
            uint64_t offset=0;QElapsedTimer drain;
            // Six copies cross the old four-image limit. Idle padding crosses the
            // old sample budget even with a short Robot fixture, without RF hardware.
            const uint64_t total=uint64_t(samples.size())*6+48000*481;
            return decodeSstvStream([&]()->SstvStreamItem {
                if(offset>=total) {
                    observed=QFileInfo::exists(QDir(path).filePath("image-5.png.json"));
                    if(observed){if(cancelAfterSave)stop=true;return SstvStreamEnd{};}
                    if(!drain.isValid())drain.start();
                    if(drain.elapsed()>10000)throw std::runtime_error("image not archived before EOF");
                    return SstvStreamIdle{};
                }
                SstvInputEvent block;block.sampleRate=48000;block.targetHz=145800000;
                block.sourceId=1;block.epoch=1;block.generation=1;block.firstSample=offset;
                block.count=uint32_t(std::min(uint64_t(SstvInputEvent::maxSamples),total-offset));
                for(size_t i=0;i<block.count;++i){const auto pos=offset+i;block.samples[i]=pos<uint64_t(samples.size())*6?samples[pos%samples.size()]:0.f;}
                offset+=block.count;return block;
            },"auto",[&]{return stop.load();},{},path);
        });
        if(cancelAfterSave) REQUIRE_THROWS_WITH(task.get(),Catch::Matchers::ContainsSubstring("cancelled"));
        else {
            const auto result=task.get();CHECK(result.savedImages>=6);CHECK(result.images.empty());CHECK(result.outputSamples>48000*480);
            const auto report=saveSstvLiveResult(result,path);CHECK(report.at("savedImages")==result.savedImages);
        }
        REQUIRE(observed);
        for(int i=0;i<6;++i){const auto file=QDir(path).filePath(QString("image-%1.png").arg(i));CHECK_FALSE(QImage(file).isNull());CHECK(QFileInfo::exists(file+".json"));}
    }
}
