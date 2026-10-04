#include "SstvWindow.h"
#include "SstvImageFile.h"
#include "SstvLiveSession.h"
#include "miniaudio.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QLineEdit>
#include <QSettings>

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

TEST_CASE("SSTV radio selection preserves stable identity and distinguishes receiver taps", "[sstv-live-gui][ownership]") {
    QSettings().setValue("sstv/deviceKey","missing-radio");
    SstvWindow window(decodeSstvImageFile);
    window.setLiveSource([](const auto&,const auto&,const auto&) { return SstvWindow::Decode{}; });
    window.setRfDevices({{"radio-a","SDR A"},{"radio-b","SDR B"}});
    auto* combo=window.findChild<QComboBox*>("sstvDevice");
    auto* frequency=window.findChild<QDoubleSpinBox*>("sstvFrequency");
    REQUIRE(combo); REQUIRE(frequency);
    CHECK(window.selectedDeviceKey()=="missing-radio");
    CHECK(combo->currentText().contains("Unavailable"));
    const auto source=window.findChild<QComboBox*>("sstvSource");
    source->setCurrentIndex(source->findData("live"));
    combo->setCurrentIndex(combo->findData(QString()));
    CHECK_FALSE(frequency->isEnabled());
    combo->setCurrentIndex(combo->findData("radio-b"));
    CHECK(frequency->isEnabled());
    frequency->setValue(145.8);
    CHECK(window.selectedFrequencyHz()==145800000.0);
    window.setRfDevices({{"radio-b","Renamed B"},{"radio-a","SDR A"}});
    CHECK(window.selectedDeviceKey()=="radio-b");
    CHECK_FALSE(window.selectRfSource("missing-radio", 145800000.0));
    CHECK_FALSE(window.selectRfSource("radio-a", -1.0));
    CHECK(window.selectedDeviceKey()=="radio-b");
    REQUIRE(window.selectRfSource("radio-a", 145900000.0));
    CHECK(window.selectedDeviceKey()=="radio-a");
    CHECK(window.selectedFrequencyHz()==145900000.0);
    QSettings().remove("sstv/deviceKey");
}

TEST_CASE("Named SSTV sessions keep separate radios settings and cancellation", "[sstv-live-gui][ownership]") {
    const QString firstId = "ownership_test_a", secondId = "ownership_test_b";
    struct SettingsCleanup {
        ~SettingsCleanup() {
            QSettings().remove("sstv/instances/ownership_test_a");
            QSettings().remove("sstv/instances/ownership_test_b");
        }
    } cleanup;
    QSettings().remove("sstv/instances/ownership_test_a");
    QSettings().remove("sstv/instances/ownership_test_b");
    CHECK_FALSE(SstvWindow::validSessionId("../escape"));
    CHECK_FALSE(SstvWindow::validSessionId("radio\n"));
    CHECK_FALSE(SstvWindow::validSessionId(QString(65,'a')));
    CHECK(SstvWindow::validSessionId({}));
    CHECK(SstvWindow::sessionObjectName("Radio_A")==SstvWindow::sessionObjectName("radio_a"));
    CHECK_THROWS_AS(SstvWindow(decodeSstvImageFile,nullptr,"../escape"), std::invalid_argument);
    QTemporaryDir directory; REQUIRE(directory.isValid());
    std::atomic<int> running{0};
    auto source = [&running](const auto& finish,const auto&,const auto&) -> SstvWindow::Decode {
        return [&running,finish](const auto&,const auto& output,const auto&,const auto& cancel,const auto&) -> nlohmann::json {
            ++running;
            while(!cancel() && !finish->load()) QThread::msleep(1);
            --running;
            return {{"outputDirectory",output.toStdString()},{"images",nlohmann::json::array()}};
        };
    };
    auto until = [](auto predicate) {
        QElapsedTimer timer; timer.start();
        while(!predicate() && timer.elapsed()<3000) {QApplication::processEvents();QThread::msleep(1);}
        return predicate();
    };
    {
        SstvWindow first(decodeSstvImageFile,nullptr,firstId), second(decodeSstvImageFile,nullptr,secondId);
        for(auto* window:{&first,&second}) {
            window->setLiveSource(source);
            window->setRfDevices({{"radio-a","A"},{"radio-b","B"}});
            window->show();
        }
        REQUIRE(first.selectRfSource("radio-a",145800000));
        REQUIRE(second.selectRfSource("radio-b",433400000));
        REQUIRE(first.startLive(directory.filePath("a"),"auto"));
        REQUIRE(second.startLive(directory.filePath("b"),"auto"));
        REQUIRE(until([&]{return running==2;}));
        first.hide(); QApplication::processEvents();
        CHECK(running==2); CHECK(first.busy()); CHECK(second.busy());
        first.cancel(); REQUIRE(wait(first));
        CHECK(running==1); CHECK(second.busy());
        second.finishLive(); REQUIRE(wait(second)); CHECK(running==0);
    }
    SstvWindow first(decodeSstvImageFile,nullptr,firstId), second(decodeSstvImageFile,nullptr,secondId);
    for(auto* window:{&first,&second}) window->setRfDevices({{"radio-b","B"},{"radio-a","A"}});
    CHECK(first.selectedDeviceKey()=="radio-a"); CHECK(second.selectedDeviceKey()=="radio-b");
    CHECK(first.selectedFrequencyHz()==145800000); CHECK(second.selectedFrequencyHz()==433400000);
    CHECK(first.objectName()!=second.objectName());
}

TEST_CASE("Live SSTV GUI finish saves and cancel releases its receiver","[sstv-live-gui]") {
    backend();
    for(bool cancel:{false,true}) {
        QTemporaryDir directory; REQUIRE(directory.isValid());
        auto feed=std::make_shared<SstvReceiverFeed>();
        SstvWindow window(decodeSstvImageFile);
        window.setLiveSource([feed](const auto& finish,const auto&,const auto&)->SstvWindow::Decode {
            return [feed,finish](const auto&,const auto& output,const auto& mode,const auto& cancel,const auto& preview) {
                return decodeSstvLive(feed,[]{},output,mode,[finish]{return finish->load();},cancel,preview);
            };
        });
        window.show();
        REQUIRE(window.startLive(directory.filePath("live"),"auto"));
        CHECK_FALSE(window.selectRfSource({}, 145800000.0));
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

TEST_CASE("SSTV GUI separates RF route from image format", "[sstv-live-gui]") {
    SstvWindow window(decodeSstvImageFile);
    QString requested;
    window.setLiveSource([&](const auto&,const QString& rfMode,const auto& status)->SstvWindow::Decode {
        requested=rfMode;
        return [status](const auto&,const auto& output,const auto&,const auto&,const auto&)->nlohmann::json {
            status("LSB");
            return {{"outputDirectory",output.toStdString()},{"images",nlohmann::json::array()}};
        };
    });
    window.show();
    CHECK_FALSE(window.findChild<QComboBox*>("sstvRfMode")->isEnabled());
    CHECK_FALSE(window.startLive("unused","auto","bogus"));
    REQUIRE(window.startLive("unused","robot36","LSB"));
    CHECK(requested=="LSB"); CHECK(window.selectedMode()=="robot36");
    CHECK_FALSE(window.findChild<QComboBox*>("sstvRfMode")->isEnabled());
    REQUIRE(wait(window));
    CHECK(window.detectedRfMode()=="LSB");
    CHECK(window.findChild<QComboBox*>("sstvRfMode")->isEnabled());
    const auto screenshot=qEnvironmentVariable("SDR_TOWN_SSTV_RF_SCREENSHOT");
    if(!screenshot.isEmpty()) CHECK(window.grab().save(screenshot));
}

TEST_CASE("SSTV save root is remembered and sessions never reuse directories", "[sstv-live-gui]") {
    QTemporaryDir directory;REQUIRE(directory.isValid());
    QSettings settings;const auto previous=settings.value("sstv/saveFolder");
    struct Restore {QVariant value;~Restore(){QSettings s;if(value.isValid())s.setValue("sstv/saveFolder",value);else s.remove("sstv/saveFolder");}} restore{previous};
    QStringList outputs;
    SstvWindow window([&](const auto&,const QString& output,const auto&,const auto&,const auto&)->nlohmann::json {
        outputs<<output;
        if(!QDir().mkdir(output))throw std::runtime_error("session already exists");
        return {{"outputDirectory",output.toStdString()},{"images",nlohmann::json::array()}};
    });
    window.findChild<QLineEdit*>("sstvInput")->setText("fixture.wav");
    window.findChild<QLineEdit*>("sstvOutput")->setText(directory.filePath("saved"));
    for(int i=0;i<2;++i){window.findChild<QPushButton*>("sstvDecode")->click();REQUIRE(wait(window));}
    REQUIRE(outputs.size()==2);CHECK(outputs[0]!=outputs[1]);
    CHECK(QFileInfo(outputs[0]).absolutePath()==directory.filePath("saved"));
    SstvWindow reopened(decodeSstvImageFile);
    CHECK(reopened.findChild<QLineEdit*>("sstvOutput")->text()==directory.filePath("saved"));
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
    window.setLiveSource([&](const auto& finish,const auto&,const auto&)->SstvWindow::Decode {
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
    const auto* images=window.findChild<QListWidget*>("sstvImages");
    REQUIRE_FALSE(expected.at("images").empty());
    REQUIRE(images->count()==expected.at("images").size());
    for (const auto& image : expected.at("images")) {
        const auto file=QString::fromStdString(image.at("file").get<std::string>());
        CHECK(QImage(directory.filePath("live/"+file))==QImage(directory.filePath("reference/"+file)));
    }
    CHECK_FALSE(window.findChild<QLabel*>("sstvPreview")->pixmap().isNull());
    const auto screenshot=qEnvironmentVariable("SDR_TOWN_SSTV_LIVE_SCREENSHOT");
    if(!screenshot.isEmpty()) {
        REQUIRE(window.grab().save(screenshot));
        window.resize(560,420); QApplication::processEvents();
        REQUIRE(window.grab().save(screenshot+".small.png"));
    }
    auto next=feed->attach();feed->detach(next);window.close();
}
