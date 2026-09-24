#include "InmarsatReplayDialog.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QTemporaryDir>
#include <QToolButton>
#include <QLineEdit>
#include <QSlider>
#include <QThread>

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
