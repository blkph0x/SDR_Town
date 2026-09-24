#include "InmarsatReplayDialog.h"
#include "InmarsatMapWidget.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QTemporaryDir>
#include <QToolButton>
#include <QLineEdit>
#include <QSlider>
#include <QThread>
#include <QFile>

TEST_CASE("Inmarsat IQ GUI runs shared replay and preserves responsive controls", "[inmarsat][gui]") {
    QTemporaryDir dir; REQUIRE(dir.isValid());
    QFile iq(dir.filePath("iq.bin")); REQUIRE(iq.open(QIODevice::WriteOnly));
    REQUIRE(iq.write(QByteArray(96000 * 2, char(127))) == 192000); iq.close();
    InmarsatReplayDialog dialog; dialog.show();
    InmarsatReplayOptions options;
    options.path = iq.fileName(); options.input = {"cu8", 48000, 1542935000}; options.logDirectory = dir.path();
    dialog.startReplay(options);
    auto spin = [&](const std::function<bool()>& done) {
        for (int i = 0; i < 1000 && !done(); ++i) { QApplication::processEvents(); QThread::msleep(2); }
        REQUIRE(done());
    };
    spin([&] { return dialog.snapshot().position > 0; });
    auto* pause = dialog.findChild<QToolButton*>("iqPause"); REQUIRE(pause);
    spin([&] { return pause->isEnabled(); }); pause->click();
    spin([&] { return dialog.snapshot().state == "paused"; });
    REQUIRE_FALSE(dialog.findChild<QLineEdit*>("iqPath")->isEnabled());
    auto* play = dialog.findChild<QToolButton*>("iqPlay"); REQUIRE(play);
    spin([&] { return play->isEnabled(); }); play->click();
    spin([&] { return !dialog.snapshot().running; });
    CHECK(dialog.snapshot().state == "complete");
    CHECK(dialog.snapshot().position == 96000);
    CHECK(dialog.snapshot().pipeline["pcmSamples"] == 0);
    spin([&] { return dialog.findChild<QLineEdit*>("iqPath")->isEnabled(); });
    CHECK_FALSE(dialog.findChild<QSlider*>("iqPosition")->isEnabled());
    const auto screenshot = qEnvironmentVariable("SDR_TOWN_INMARSAT_SCREENSHOT");
    if (!screenshot.isEmpty()) REQUIRE(dialog.grab().save(screenshot));
}
TEST_CASE("Inmarsat map renders offline and bounds positions", "[inmarsat][gui]") {
    InmarsatMapWidget map;map.resize(900,460);map.show();
    const nlohmann::json position={{"aesId",0x123456},{"latDeg",-34.4},{"lonDeg",150.9},
        {"registration","TEST001"},{"callsign","TEST"},{"altitudeFt",32000},{"secondsPastHour",1800}};
    map.setReport({{"positions",{position}},{"voiceAesId",0x123456},{"voiceActive",true}},true);
    CHECK(map.aircraftCount()==1);QApplication::processEvents();
    const auto image=map.grab().toImage();CHECK(!image.isNull());
    int green=0,land=0;
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
        const auto c=image.pixelColor(x,y);if(c==QColor("#52ef88"))++green;if(c==QColor("#3b5148"))++land;
    }
    CHECK(green>10);CHECK(land>10000);
    const auto path=qEnvironmentVariable("SDR_TOWN_INMARSAT_MAP_SCREENSHOT");
    if(!path.isEmpty())REQUIRE(image.save(path));
    auto invalid=position;invalid["latDeg"]=91;
    map.setReport({{"positions",{invalid}}},false);CHECK(map.aircraftCount()==0);
}
TEST_CASE("Aero public burst recording reaches the GUI map", "[inmarsat][gui][reference]") {
    const auto iq=qEnvironmentVariable("SDR_TOWN_AERO_BURST_IQ");
    if(iq.isEmpty())SKIP("Optional independent JAERO burst IQ fixture not supplied");
    QTemporaryDir dir;InmarsatReplayDialog dialog;dialog.show();
    InmarsatReplayOptions options;options.path=iq;options.realTime=false;
    options.mode=InmarsatDemodMode::AeroBurstOqpsk10500;options.logDirectory=dir.path();
    dialog.startReplay(options);
    for(int i=0;i<15000 && dialog.snapshot().running;++i){QApplication::processEvents();QThread::msleep(2);}
    const auto s=dialog.snapshot();REQUIRE_FALSE(s.running);REQUIRE(s.state=="complete");
    CHECK(s.pipeline["validatedFrames"]==8);REQUIRE(s.pipeline["positions"].size()==2);
    auto* map=dialog.findChild<InmarsatMapWidget*>("inmarsatMap");REQUIRE(map);
    for(int i=0;i<200 && map->aircraftCount()!=2;++i){QApplication::processEvents();QThread::msleep(2);}
    REQUIRE(map->aircraftCount()==2);
    const auto path=qEnvironmentVariable("SDR_TOWN_AERO_REFERENCE_SCREENSHOT");
    if(!path.isEmpty())REQUIRE(dialog.grab().save(path));
}
