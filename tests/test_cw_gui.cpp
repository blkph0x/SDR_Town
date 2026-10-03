#include "CwWindow.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QThread>
#include <atomic>

namespace {
bool waitFor(const std::function<bool()>& predicate) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 3000) {QApplication::processEvents(); QThread::msleep(5);}
    return predicate();
}
}
TEST_CASE("Morse window stays responsive and cancels its owned worker", "[cw][gui]") {
    std::atomic<bool> stopped = false;
    CwWindow window([&] {return [&](CwOptions, const CwCancel& cancel, const CwPublish& publish) {
        CwProgress p; p.decoder.text = "CQ TEST"; p.decoder.samples = 48000;
        p.source = "Test source"; p.status = "Receiving";
        publish(p);
        while (!cancel()) QThread::msleep(1);
        stopped = true;
    };});
    window.show();
    window.findChild<QDoubleSpinBox*>("cwPitch")->setValue(0);
    window.findChild<QDoubleSpinBox*>("cwSpeed")->setValue(0);
    auto* start = window.findChild<QPushButton*>("cwStart");
    start->click();
    REQUIRE_FALSE(start->isEnabled());
    REQUIRE(waitFor([&] {return window.findChild<QPlainTextEdit*>("cwText")->toPlainText() == "CQ TEST";}));
    window.close();
    REQUIRE(waitFor([&] {return !window.isVisible();}));
    REQUIRE(stopped);
}
TEST_CASE("Morse window reports bad sources and validates manual estimates", "[cw][gui]") {
    CwWindow window({});
    window.show();
    auto* start = window.findChild<QPushButton*>("cwStart");
    auto* status = window.findChild<QLabel*>("cwStatus");
    window.findChild<QDoubleSpinBox*>("cwPitch")->setValue(100);
    start->click();
    REQUIRE(status->text().contains("200..1200"));
    window.findChild<QDoubleSpinBox*>("cwPitch")->setValue(0);
    window.findChild<QDoubleSpinBox*>("cwSpeed")->setValue(0);
    window.findChild<QComboBox*>("cwSource")->setCurrentIndex(1);
    window.findChild<QLineEdit*>("cwFile")->setText("missing-cw-file.wav");
    start->click();
    REQUIRE(waitFor([&] {return start->isEnabled();}));
    REQUIRE(status->text().contains("Choose an audio recording"));
}
