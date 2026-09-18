#include "SstvWindow.h"
#include "SstvImageFile.h"
#include "SstvLiveSession.h"
#include "miniaudio.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

namespace {
void backend() {
    const bool present=QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath("sdrtown_sstv.exe"));
#ifdef SDR_TOWN_TEST_SSTV_BACKEND
    REQUIRE(present);
#else
    if(!present) SKIP("Optional SSTV backend not enabled");
#endif
}
bool wait(SstvWindow& window) {
    QElapsedTimer clock; clock.start();
    while(window.busy() && clock.elapsed()<20000) {QApplication::processEvents();QThread::msleep(1);}
    if(window.busy()) window.cancel();
    return !window.busy();
}
}

TEST_CASE("Live SSTV GUI finish saves and cancel releases its receiver","[sstv-live-gui]") {
    backend();
    for(bool cancel:{false,true}) {
        QTemporaryDir directory; REQUIRE(directory.isValid());
        auto feed=std::make_shared<SstvReceiverFeed>();
        SstvWindow window(decodeSstvImageFile);
        window.setLiveSource([feed](const auto& finish)->SstvWindow::Decode {
            return [feed,finish](const auto&,const auto& output,const auto& mode,const auto& cancel,const auto& preview) {
                return decodeSstvLive(feed,[]{},output,mode,[finish]{return finish->load();},cancel,preview);
            };
        });
        window.show();
        REQUIRE(window.startLive(directory.filePath("live"),"auto"));
        REQUIRE_FALSE(window.startLive(directory.filePath("duplicate"),"auto"));
        REQUIRE_FALSE(window.findChild<QComboBox*>("sstvSource")->isEnabled());
        QTimer::singleShot(100,&window,[&]{if(cancel) window.close();else window.finishLive();});
        REQUIRE(wait(window));
        CHECK(QFileInfo::exists(directory.filePath("live/sstv-report.json"))==!cancel);
        CHECK(window.findChild<QComboBox*>("sstvSource")->isEnabled());
        auto next=feed->attach(); feed->detach(next);
        window.close();
    }
}

TEST_CASE("Live SSTV window saves independently verified streamed images","[sstv-live-gui-recording]") {
    const auto input=qEnvironmentVariable("SDR_TOWN_SSTV_STREAM_INPUT");
    if(input.isEmpty()) SKIP("Recording supplied by test_sstv_worker.py");
    backend();
    const auto reference=qEnvironmentVariable("SDR_TOWN_SSTV_STREAM_REFERENCE");
    const auto mode=qEnvironmentVariable("SDR_TOWN_SSTV_STREAM_MODE","auto");
    QTemporaryDir directory; REQUIRE(directory.isValid());
    const auto expected=decodeSstvImageFile(reference,directory.filePath("reference"),mode);
    ma_decoder decoder{}; const auto config=ma_decoder_config_init(ma_format_f32,1,0);
    REQUIRE(ma_decoder_init_file_w(input.toStdWString().c_str(),&config,&decoder)==MA_SUCCESS);
    struct Guard {ma_decoder* decoder;~Guard(){ma_decoder_uninit(decoder);}} guard{&decoder};
    ma_uint64 length=0; REQUIRE(ma_decoder_get_length_in_pcm_frames(&decoder,&length)==MA_SUCCESS);
    REQUIRE(length<=uint64_t{decoder.outputSampleRate}*360);
    std::vector<float> samples(size_t(length),0.f); ma_uint64 read=0;
    const auto status=ma_decoder_read_pcm_frames(&decoder,samples.data(),length,&read);
    REQUIRE((status==MA_SUCCESS || status==MA_AT_END)); REQUIRE(read==length);
    auto feed=std::make_shared<SstvReceiverFeed>();
    SstvWindow window(decodeSstvImageFile);
    window.setLiveSource([&](const auto& finish)->SstvWindow::Decode {
        return [&,finish](const auto&,const auto& output,const auto& mode,const auto& cancel,const auto& preview) {
            size_t offset=0; bool beforeAttach=true;
            // Deterministic source advances one block per worker poll, through the
            // same receiver feed used by RX. This is a recording, not an RF test.
            const auto source=[&] {
                if(beforeAttach) {beforeAttach=false;return;}
                if(offset==samples.size()) {
                    const auto stats=feed->stats();
                    if(stats && stats->queuedBlocks==0) finish->store(true);
                    return;
                }
                const auto count=std::min(size_t{8192},samples.size()-offset);
                FmMultiplexBlock block;
                block.samples.assign(samples.begin()+offset,samples.begin()+offset+count);
                block.firstSample=offset; block.epoch=1;block.discontinuity=false;
                block.sampleRate=decoder.outputSampleRate;block.targetHz=145800000;
                feed->publish(block,9); offset+=count;
            };
            return decodeSstvLive(feed,source,output,mode,[finish]{return finish->load();},cancel,preview);
        };
    });
    window.show(); REQUIRE(window.startLive(directory.filePath("live"),mode));
    REQUIRE(wait(window));
    const auto* images=window.findChild<QListWidget*>("sstvImages"); REQUIRE(images->count()==1);
    const auto file=QString::fromStdString(expected.at("images")[0].at("file").get<std::string>());
    CHECK(QImage(directory.filePath("live/"+file))==QImage(directory.filePath("reference/"+file)));
    CHECK_FALSE(window.findChild<QLabel*>("sstvPreview")->pixmap().isNull());
    const auto screenshot=qEnvironmentVariable("SDR_TOWN_SSTV_LIVE_SCREENSHOT");
    if(!screenshot.isEmpty()) {
        REQUIRE(window.grab().save(screenshot));
        window.resize(560,420); QApplication::processEvents();
        REQUIRE(window.grab().save(screenshot+".small.png"));
    }
    auto next=feed->attach();feed->detach(next);window.close();
}
