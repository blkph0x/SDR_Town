#include "DtmfWindow.h"
#include "DtmfReport.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QDataStream>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <numbers>

namespace {
bool waitFor(const std::function<bool()>& ready) {
    QElapsedTimer t; t.start();
    while (!ready() && t.elapsed()<4000) {QApplication::processEvents();QThread::msleep(5);}
    return ready();
}
QString fixture(QTemporaryDir& dir) {
    const auto path=dir.filePath(QString::fromUtf8("DTMF \xc3\xa9 test.wav"));
    QFile file(path); REQUIRE(file.open(QIODevice::WriteOnly));
    QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
    constexpr unsigned samples=8000*.020;
    out.writeRawData("RIFF",4);out<<quint32(36+2*samples);out.writeRawData("WAVEfmt ",8);
    out<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(8000)<<quint32(16000)<<quint16(2)<<quint16(16);
    out.writeRawData("data",4);out<<quint32(2*samples);
    for (unsigned i=0;i<samples;++i)
        out<<qint16(6000*(std::sin(2*std::numbers::pi*(3300-697)*i/8000)+std::sin(2*std::numbers::pi*(3300-1209)*i/8000)));
    return path;
}
}
TEST_CASE("DTMF GUI and direct decoder agree on short inverted Unicode recording", "[dtmf][gui]") {
    QTemporaryDir dir; REQUIRE(dir.isValid());const auto path=fixture(dir);
    DtmfOptions options;options.fast=true;options.inverted=true;
    const auto expected=decodeDtmfFile(path.toUtf8().toStdString(),13,options);
    REQUIRE(expected.lastSequence=="1");REQUIRE(expected.samples==160);
    REQUIRE(dtmfReport(expected)["detections"][0]["options"]["inverted"]==true);
    REQUIRE_THROWS(decodeDtmfFile(path.toUtf8().toStdString(),13,options,[]{return true;}));
    DtmfWindow window({});window.show();
    window.findChild<QComboBox*>("dtmfSource")->setCurrentIndex(2);
    window.findChild<QComboBox*>("dtmfProfile")->setCurrentIndex(1);
    window.findChild<QComboBox*>("dtmfTransform")->setCurrentIndex(1);
    window.findChild<QDoubleSpinBox*>("dtmfPivot")->setValue(3300);
    window.findChild<QDoubleSpinBox*>("dtmfScale")->setValue(1);
    window.findChild<QDoubleSpinBox*>("dtmfShift")->setValue(0);
    window.findChild<QLineEdit*>("dtmfFile")->setText(path);
    auto* analyze=window.findChild<QPushButton*>("dtmfAnalyze");analyze->click();
    REQUIRE_FALSE(analyze->isEnabled());
    REQUIRE(waitFor([&]{return analyze->isEnabled();}));
    const auto* history=window.findChild<QTableWidget*>("dtmfHistory");
    REQUIRE(history->rowCount()==1);REQUIRE(history->item(0,0)->text()=="1");
    REQUIRE(history->item(0,3)->text().toULongLong()==expected.history[0].confirmedSample);
    const auto screenshots=qEnvironmentVariable("SDR_TOWN_DTMF_SCREENSHOTS");
    if (!screenshots.isEmpty()) {
        REQUIRE(QDir().mkpath(screenshots));
        QApplication::processEvents();
        REQUIRE(window.grab().save(screenshots+"/default.png"));
        window.resize(540,380);QApplication::processEvents();
        REQUIRE(window.grab().save(screenshots+"/compact.png"));
    }
    window.findChild<QLineEdit*>("dtmfFile")->setText("nonexistent-dtmf-file.wav");analyze->click();
    REQUIRE(waitFor([&]{return analyze->isEnabled();}));
    REQUIRE(history->rowCount()==0);
    REQUIRE(window.findChild<QLabel*>("dtmfStatus")->text().contains("Cannot open"));
}
TEST_CASE("DTMF GUI options apply to both observers without decoding or consuming events", "[dtmf][gui]") {
    auto main=std::make_shared<DtmfDecoder>(),input=std::make_shared<DtmfDecoder>();
    DtmfWindow window([&](bool in){return in?input:main;});window.show();
    window.findChild<QComboBox*>("dtmfProfile")->setCurrentIndex(1);
    window.findChild<QComboBox*>("dtmfTransform")->setCurrentIndex(0);
    window.findChild<QDoubleSpinBox*>("dtmfScale")->setValue(1);
    window.findChild<QDoubleSpinBox*>("dtmfShift")->setValue(0);
    window.findChild<QPushButton*>("dtmfApply")->click();
    REQUIRE(main->options().fast);REQUIRE(input->options().fast);
    REQUIRE(main->snapshot().samples==0);REQUIRE(input->snapshot().samples==0);
    REQUIRE(waitFor([&]{return window.findChild<QLabel*>("dtmfStatus")->text().contains("No recent");}));
    window.findChild<QComboBox*>("dtmfProfile")->setCurrentIndex(0);
    window.findChild<QPushButton*>("dtmfApply")->click();
    REQUIRE_FALSE(main->options().fast);
}
