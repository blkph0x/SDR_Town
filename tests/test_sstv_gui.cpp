#include "SstvWindow.h"
#include "SstvImageFile.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>

namespace {
bool finish(SstvWindow& window) {
    QElapsedTimer clock; clock.start();
    while(window.busy() && clock.elapsed()<15000) {QApplication::processEvents(); QThread::msleep(2);}
    QApplication::processEvents();
    return !window.busy();
}
}

TEST_CASE("SSTV GUI single job remains responsive and cancels on close", "[sstv-gui]") {
    std::atomic<bool> entered=false,wrongThread=false;
    SstvWindow window([&](const auto&,const auto&,const auto&,const auto& cancelled,const auto&)->nlohmann::json {
        wrongThread=QThread::currentThread()==qApp->thread(); entered=true;
        while(!cancelled()) QThread::msleep(2);
        throw std::runtime_error("SSTV decode cancelled");
    });
    window.show();
    REQUIRE_FALSE(window.startDecode("","out","auto"));
    REQUIRE(window.startDecode("input","out","auto"));
    REQUIRE_FALSE(window.startDecode("input","out2","auto"));
    REQUIRE_FALSE(window.findChild<QPushButton*>("sstvDecode")->isEnabled());
    int ticks=0;
    QTimer timer;
    QObject::connect(&timer,&QTimer::timeout,[&]{++ticks; if(ticks==5) window.close();});
    timer.start(2);
    REQUIRE(finish(window));
    CHECK(entered); CHECK_FALSE(wrongThread); CHECK(ticks>=5);
    CHECK_FALSE(window.isVisible());
    CHECK(window.findChild<QLabel*>("sstvStatus")->text().contains("cancelled"));
    CHECK(window.findChild<QPushButton*>("sstvDecode")->isEnabled());
}

TEST_CASE("SSTV GUI error and empty results clear prior state", "[sstv-gui]") {
    int calls=0;
    SstvWindow window([&](const auto&,const auto&,const auto&,const auto&,const auto&)->nlohmann::json {
        if(calls++==0) throw std::runtime_error("bad recording");
        return {{"outputDirectory","test-output"},{"images",nlohmann::json::array()}};
    });
    REQUIRE(window.startDecode("input","out","auto")); REQUIRE(finish(window));
    CHECK(window.findChild<QLabel*>("sstvStatus")->text()=="bad recording");
    REQUIRE(window.startDecode("input","out2","auto")); REQUIRE(finish(window));
    CHECK(window.findChild<QLabel*>("sstvStatus")->text()=="No images detected");
    CHECK(window.findChild<QListWidget*>("sstvImages")->count()==0);
    CHECK_FALSE(window.findChild<QPushButton*>("sstvCancel")->isEnabled());
}

TEST_CASE("SSTV cancellation precedes file access", "[sstv-gui]") {
    CHECK_THROWS_WITH(decodeSstvImageFile("missing.wav","unused","auto",[]{return true;}),"SSTV decode cancelled");
}

TEST_CASE("SSTV parent teardown joins its cancelled worker", "[sstv-gui]") {
    std::atomic<bool> stopped=false;
    {
        SstvWindow window([&](const auto&,const auto&,const auto&,const auto& cancelled,const auto&)->nlohmann::json {
            while(!cancelled()) QThread::msleep(1);
            stopped=true;
            throw std::runtime_error("cancelled");
        });
        REQUIRE(window.startDecode("input","output","auto"));
    }
    CHECK(stopped);
    QApplication::processEvents(); // Any queued completion must not reference the destroyed window.
}

TEST_CASE("SSTV progressive preview is responsive and cleared on cancellation", "[sstv-gui]") {
    SstvWindow window([](const auto&,const auto&,const auto&,const auto& cancelled,const auto& preview)->nlohmann::json {
        QImage image(320,240,QImage::Format_RGB888); image.fill(Qt::green);
        for(int i=0;i<1000;++i) preview(image,"robot36",8);
        while(!cancelled()) QThread::msleep(1);
        throw std::runtime_error("cancelled");
    });
    window.show();
    REQUIRE(window.startDecode("input","output","auto"));
    bool shown=false;
    QTimer::singleShot(200,&window,[&] {
        shown=window.findChild<QLabel*>("sstvStatus")->text().contains("8/240") &&
              !window.findChild<QLabel*>("sstvPreview")->pixmap().isNull();
        window.cancel();
    });
    REQUIRE(finish(window)); CHECK(shown);
    CHECK(window.findChild<QLabel*>("sstvPreview")->pixmap().isNull());
}

TEST_CASE("SSTV GUI actual recording matches direct file decoder", "[sstv-gui-recording]") {
    const auto input=qEnvironmentVariable("SDR_TOWN_SSTV_GUI_FIXTURE");
    if(input.isEmpty()) SKIP("Independent recording supplied by test_sstv_gui.py");
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto expected=decodeSstvImageFile(input,directory.filePath("direct"),"auto");
    REQUIRE(expected.at("images").size()==1);
    const bool partial=qEnvironmentVariable("SDR_TOWN_SSTV_GUI_PARTIAL")=="1";
    CHECK(expected.at("images")[0].at("complete").get<bool>()==!partial);
    std::atomic<int> previews=0;
    SstvWindow window([&](const auto& input,const auto& output,const auto& mode,const auto& cancel,const auto& preview) {
        return decodeSstvImageFile(input,output,mode,cancel,[&](const auto& image,const auto& found,int rows) {
            ++previews; preview(image,found,rows);
        });
    });
    window.show();
    int ticks=0; QTimer heartbeat;
    QObject::connect(&heartbeat,&QTimer::timeout,[&]{++ticks;}); heartbeat.start(1);
    REQUIRE(window.startDecode(input,directory.filePath("gui"),"auto"));
    REQUIRE(finish(window));
    CHECK(previews>1);
    const auto* list=window.findChild<QListWidget*>("sstvImages");
    REQUIRE(list->count()==1); CHECK(ticks>0);
    CHECK(list->item(0)->text().contains(partial?"Partial":"Complete"));
    const auto name=QString::fromStdString(expected.at("images")[0].at("file").get<std::string>());
    CHECK(QImage(directory.filePath("gui/"+name))==QImage(directory.filePath("direct/"+name)));
    CHECK_FALSE(window.findChild<QLabel*>("sstvPreview")->pixmap().isNull());
    const auto screenshot=qEnvironmentVariable("SDR_TOWN_SSTV_GUI_SCREENSHOT");
    if(!screenshot.isEmpty()) {
        REQUIRE(window.grab().save(screenshot));
        window.resize(560,420); QApplication::processEvents();
        REQUIRE(window.grab().save(screenshot+".small.png"));
    }
    window.close();
}
