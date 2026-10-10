#include "AntennaControlWindow.h"
#include "frontend/DvbSurvey.h"
#include "frontend/EquipmentWizard.h"
#include "frontend/FrontEndMetrics.h"
#include "frontend/FrontEndPower.h"
#include "frontend/LinkBudgetHint.h"
#include "frontend/LnbConversion.h"
#include "frontend/PassArming.h"
#include "frontend/RotatorTrack.h"
#include "frontend/StationPassSession.h"
#include "frontend/StationProfile.h"
#include "frontend/StationRadio.h"
#include "frontend/StationSky.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSpinBox>
#include <QRegularExpression>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <cmath>
#include <complex>
#include <vector>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("LNB noise temperature matches the published 0.3 dB example") {
    const double kelvin = noiseTempFromNoiseFigureDb(0.3);
    CHECK(kelvin > 20.0);
    CHECK(kelvin < 22.0);
    CHECK(claimedNoiseFigureNeedsCaution(0.1));
    CHECK_FALSE(claimedNoiseFigureNeedsCaution(0.3));
}

TEST_CASE("Universal Ku low and high band IF stay inside 950-2150 MHz") {
    LnbProfile lnb;
    const auto low = lnbTune(lnb, 11.7e9, false, false);
    REQUIRE(low.ok);
    CHECK(low.voltageV == 13);
    CHECK_FALSE(low.tone22kHz);
    CHECK(std::abs(low.ifHz - 1.95e9) < 1.0);
    const auto high = lnbTune(lnb, 12.2e9, true, true);
    REQUIRE(high.ok);
    CHECK(high.voltageV == 18);
    CHECK(high.tone22kHz);
    CHECK(std::abs(high.ifHz - 1.6e9) < 1.0);
    CHECK_FALSE(lnbTune(lnb, 1.0e9, false, false).ok);
}

TEST_CASE("Bias-T stays off without confirmation and refuses a second source") {
    FrontEndPower power;
    std::string error;
    CHECK_FALSE(power.enable(false, 18, true, &error));
    CHECK_FALSE(power.enabled());
    CHECK_FALSE(power.enable(true, 18, true, &error));
    REQUIRE(power.selectBackend(BiasBackend::External, &error));
    REQUIRE(power.enable(true, 18, true, &error));
    CHECK(power.enabled());
    CHECK_FALSE(power.selectBackend(BiasBackend::SdrInternal, &error));
    power.noteMeasurement(18.0, 600.0, 500.0);
    CHECK_FALSE(power.enabled());
    REQUIRE(power.audit().size() >= 2);
    CHECK(power.audit().back().reason == "over-current");
}

TEST_CASE("Tracking tick clamps, pauses for jog, and parks by teardown order") {
    TrackLimits limits;
    const auto paused = planTrackTick(true, true, true, true, limits, 20, 30, 0, 0);
    CHECK_FALSE(paused.send);
    const auto clamped = planTrackTick(true, true, true, false, limits, 400, 30, 1.0, 10);
    REQUIRE(clamped.send);
    CHECK(clamped.az == 360.0);
    CHECK(clamped.clamped);
    const auto teardown = planPassTeardown();
    REQUIRE(teardown.size() == 5);
    CHECK(teardown.front() == "stop-worker");
    CHECK(teardown[1] == "rotator-stop");
    CHECK(teardown[2] == "rotator-park");
    CHECK(teardown[3] == "power-off");
}

TEST_CASE("Arm checklist fail-closes and a complete checklist is ordered") {
    PassChecklist check;
    const auto rejected = planPassArm(check, StationMission::LeoTrack);
    CHECK_FALSE(rejected.accepted);
    check.leaseHeld = true;
    check.ifInSdrSpan = true;
    check.tleFreshOrNotRequired = true;
    check.biasCurrentOk = biasRatingCoversLnb(500, 200);
    check.rotatorReadyOrOverride = true;
    check.powerConfirmed = true;
    check.diseqcConfigured = true;
    const auto arm = planPassArm(check, StationMission::LeoTrack);
    REQUIRE(arm.accepted);
    CHECK(arm.steps.front() == "diseqc");
    CHECK(arm.steps.back() == "start-worker");
    CHECK(linkMarginHint(100, 0.3, 5, 10) == LinkMarginHint::Masked);
    CHECK(linkMarginHint(100, 0.3, 40, 10) == LinkMarginHint::Good);
}

TEST_CASE("Metrics indexes are monotonic and the profile round-trips") {
    FrontEndMetrics metrics;
    metrics.add("lnb.nfDb", 0.5, "claimed");
    metrics.addText("bias", "off");
    CHECK(metrics.records()[0].sampleIndex == 1);
    CHECK(metrics.records()[1].sampleIndex == 2);
    CHECK(metrics.toJsonl().find("\"name\":\"lnb.nfDb\"") != std::string::npos);
    StationProfile profile;
    profile.dishCm = 90;
    profile.lnb.noiseFigureDb = 0.1;
    profile.mission = StationMission::GeoPark;
    StationProfile loaded;
    std::string error;
    REQUIRE(stationProfileFromJson(stationProfileToJson(profile), &loaded, &error));
    CHECK(loaded.dishCm == 90);
    CHECK(loaded.mission == StationMission::GeoPark);
    CHECK(claimedNoiseFigureNeedsCaution(loaded.lnb.noiseFigureDb));
}

TEST_CASE("D1 survey reports a tone and refuses demod and decrypt") {
    constexpr int n = 256;
    std::vector<std::complex<float>> iq(n);
    for (int i = 0; i < n; ++i)
        iq[static_cast<size_t>(i)] = std::polar(1.0f, static_cast<float>(2.0 * 3.14159265358979323846 * 8.0 * i / n));
    const auto survey = surveyIfCapture(iq.data(), iq.size(), 2.4e6);
    CHECK(survey.powerMeasured);
    CHECK(survey.peakMeasured);
    CHECK(survey.peakOffsetHz > 0.0);
    CHECK_FALSE(survey.symbolRateMeasured);
    CHECK_FALSE(survey.plsDetected);
    CHECK_FALSE(dvbDemodAvailable());
    CHECK_FALSE(commercialDecryptAvailable());
}

TEST_CASE("Clear transport-stream inventory lists PIDs and does not read scrambled payloads") {
    std::vector<std::uint8_t> bytes(188 * 2, 0);
    bytes[0] = 0x47;
    bytes[1] = 0x01;
    bytes[2] = 0x00;
    bytes[3] = 0x10;
    bytes[188] = 0x47;
    bytes[189] = 0x01;
    bytes[190] = 0x01;
    bytes[191] = 0xC0;
    const auto inventory = inventoryClearTransportStream(bytes.data(), bytes.size());
    CHECK(inventory.aligned);
    CHECK(inventory.packets == 2);
    CHECK(inventory.scrambledPackets == 1);
    CHECK(inventory.pids.size() == 2);
    CHECK_FALSE(dvbDemodAvailable());
}

TEST_CASE("Pass session commands the rotator then stop and park on abort") {
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost));
    QStringList commands;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                while (socket->canReadLine()) {
                    const auto cmd = socket->readLine();
                    commands << QString::fromLatin1(cmd).trimmed();
                    QByteArray reply;
                    if (cmd == "+p\n") reply = "get_pos:\nAzimuth: 180\nElevation: 45\nRPRT 0\n";
                    else if (cmd.startsWith("+P ")) reply = "set_pos: " + cmd.mid(3).trimmed() + "\nRPRT 0\n";
                    else if (cmd == "+S\n") reply = "stop:\nRPRT 0\n";
                    else FAIL("Unexpected command");
                    socket->write(reply);
                }
            });
        }
    });
    auto waitFor = [](const std::function<bool()>& ready) {
        QElapsedTimer timer;
        timer.start();
        while (!ready() && timer.elapsed() < 2500) {
            QApplication::processEvents();
            QThread::msleep(1);
        }
        return ready();
    };
    RotatorController rotor;
    rotor.connectTo("127.0.0.1", server.serverPort(), {});
    REQUIRE(waitFor([&] { return rotor.fresh(); }));
    REQUIRE(rotor.arm());
    StationPassSession session(rotor);
    PassChecklist check;
    std::string error;
    CHECK_FALSE(session.arm(check, {}, 20, 30, &error));
    CHECK(commands.indexOf(QRegularExpression("\\+P .*")) < 0);
    check.leaseHeld = true;
    check.ifInSdrSpan = true;
    check.tleFreshOrNotRequired = true;
    check.biasCurrentOk = true;
    check.rotatorReadyOrOverride = true;
    check.powerConfirmed = true;
    StationProfile profile;
    profile.biasBackend = BiasBackend::External;
    profile.mission = StationMission::LeoTrack;
    profile.trueRfHz = 11.7e9;
    profile.parkAz = 0;
    profile.parkEl = 0;
    REQUIRE(session.arm(check, profile, 20, 30, &error));
    REQUIRE(waitFor([&] { return commands.contains("+P 20.000 30.000"); }));
    CHECK(session.power().enabled());
    session.abort("test");
    REQUIRE(waitFor([&] { return commands.contains("+S") && commands.contains("+P 0.000 0.000"); }));
    REQUIRE(waitFor([&] { return !session.power().enabled(); }));
}

TEST_CASE("Box scan rejects a raster larger than 49 dwells") {
    const auto scan = planBoxScan(180, 40, 2, 1, 0.5);
    REQUIRE(scan.accepted);
    CHECK(scan.azimuthDeg.size() == 15);
    CHECK(scan.azimuthDeg.front() == 179);
    CHECK(scan.azimuthDeg[4] == 181);
    CHECK(scan.elevationDeg.front() == 39.5);
    CHECK(scan.elevationDeg.back() == 40.5);
    const auto huge = planBoxScan(180, 40, 8, 1, 0.5);
    CHECK_FALSE(huge.accepted);
    CHECK(huge.reject == "Box scan is larger than 49 dwells");
    RotatorController rotor;
    StationPassSession session(rotor);
    PassChecklist check;
    check.leaseHeld = true;
    check.ifInSdrSpan = true;
    check.tleFreshOrNotRequired = true;
    check.biasCurrentOk = true;
    check.rotatorReadyOrOverride = true;
    check.powerConfirmed = true;
    StationProfile profile;
    profile.biasBackend = BiasBackend::External;
    profile.mission = StationMission::GeoBoxScan;
    profile.trueRfHz = 11.7e9;
    profile.boxSpanAzDeg = 8;
    profile.boxStepDeg = 0.5;
    std::string error;
    CHECK_FALSE(session.arm(check, profile, 180, 40, &error));
    CHECK(error == "Box scan is larger than 49 dwells");
    CHECK_FALSE(session.power().enabled());
}

TEST_CASE("GEO park slews once, manual holds, and a one-point box scan completes") {
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost));
    QStringList commands;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                while (socket->canReadLine()) {
                    const auto cmd = socket->readLine();
                    commands << QString::fromLatin1(cmd).trimmed();
                    QByteArray reply;
                    if (cmd == "+p\n") reply = "get_pos:\nAzimuth: 180\nElevation: 45\nRPRT 0\n";
                    else if (cmd.startsWith("+P ")) reply = "set_pos: " + cmd.mid(3).trimmed() + "\nRPRT 0\n";
                    else if (cmd == "+S\n") reply = "stop:\nRPRT 0\n";
                    else FAIL("Unexpected command");
                    socket->write(reply);
                }
            });
        }
    });
    auto waitFor = [](const std::function<bool()>& ready) {
        QElapsedTimer timer;
        timer.start();
        while (!ready() && timer.elapsed() < 2500) {
            QApplication::processEvents();
            QThread::msleep(1);
        }
        return ready();
    };
    auto positionCommands = [&] {
        int count = 0;
        for (const auto& cmd : commands)
            if (cmd.startsWith("+P ")) ++count;
        return count;
    };
    RotatorController rotor;
    rotor.connectTo("127.0.0.1", server.serverPort(), {});
    REQUIRE(waitFor([&] { return rotor.fresh(); }));
    REQUIRE(rotor.arm());
    PassChecklist check;
    check.leaseHeld = true;
    check.ifInSdrSpan = true;
    check.tleFreshOrNotRequired = true;
    check.biasCurrentOk = true;
    check.rotatorReadyOrOverride = true;
    check.powerConfirmed = true;
    StationProfile profile;
    profile.biasBackend = BiasBackend::External;
    profile.trueRfHz = 11.7e9;
    profile.parkAz = 0;
    profile.parkEl = 0;
    std::string error;
    StationPassSession session(rotor);

    profile.mission = StationMission::GeoPark;
    REQUIRE(session.arm(check, profile, 20, 30, &error));
    REQUIRE(waitFor([&] { return commands.contains("+P 20.000 30.000"); }));
    const int afterPark = positionCommands();
    QElapsedTimer hold;
    hold.start();
    while (hold.elapsed() < 200) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    CHECK(positionCommands() == afterPark);
    CHECK(session.tunedIfHz() > 900e6);
    session.abort("test");
    REQUIRE(waitFor([&] { return !session.power().enabled(); }));

    profile.mission = StationMission::Manual;
    const int beforeManual = positionCommands();
    REQUIRE(session.arm(check, profile, 20, 30, &error));
    hold.restart();
    while (hold.elapsed() < 200) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    CHECK(positionCommands() == beforeManual);
    CHECK(session.power().enabled());
    session.abort("test");
    REQUIRE(waitFor([&] { return !session.power().enabled(); }));

    profile.mission = StationMission::GeoBoxScan;
    profile.boxSpanAzDeg = 0;
    profile.boxSpanElDeg = 0;
    profile.boxStepDeg = 0.5;
    REQUIRE(session.arm(check, profile, 15, 25, &error));
    REQUIRE(waitFor([&] { return commands.contains("+P 15.000 25.000"); }));
    bool complete = false;
    for (const auto& row : session.metrics().records())
        if (row.name == "box-scan" && row.text == "complete") complete = true;
    CHECK(complete);
    session.abort("test");
    REQUIRE(waitFor([&] { return !session.power().enabled(); }));
}

TEST_CASE("Equipment wizard defaults Bias-T off and flags a sub-0.2 dB claim") {
    EquipmentWizard wizard;
    auto* confirm = wizard.findChild<QCheckBox*>("biasConfirm");
    auto* state = wizard.findChild<QLabel*>("biasState");
    REQUIRE(confirm);
    REQUIRE(state);
    CHECK_FALSE(confirm->isChecked());
    CHECK(state->text().contains("OFF"));
    wizard.findChild<QDoubleSpinBox*>("lnbNf")->setValue(0.1);
    CHECK(wizard.findChild<QLabel*>("nfCaution")->text().contains("0.2"));
    auto* read = wizard.findChild<QPushButton*>("readPlanner");
    REQUIRE(read);
    read->click();
    CHECK(wizard.findChild<QLabel*>("skyFeed")->text().contains("No armed satellite pass"));
    CHECK(wizard.findChild<QDoubleSpinBox*>("predictAz")->value() == 180);
    CHECK_FALSE(wizard.findChild<QCheckBox*>("attestTle")->isChecked());
    CHECK_FALSE(wizard.findChild<QCheckBox*>("followPlanner")->isChecked());
    CHECK(wizard.findChild<QComboBox*>("stationMission")->currentText() == "LEO track");
    CHECK_FALSE(wizard.findChild<QCheckBox*>("horizontalPol")->isChecked());
    CHECK_FALSE(wizard.findChild<QCheckBox*>("highBand")->isChecked());
    wizard.findChild<QComboBox*>("stationMission")->setCurrentText("GEO park");
    wizard.findChild<QCheckBox*>("horizontalPol")->setChecked(true);
    wizard.findChild<QCheckBox*>("highBand")->setChecked(true);
    CHECK(wizard.profile().mission == StationMission::GeoPark);
    CHECK(wizard.profile().horizontal);
    CHECK(wizard.profile().highBand);
}

TEST_CASE("Sky lead stays one observed step ahead and crosses north the short way") {
    const auto ahead = leadSky(10, 20, 12, 21, 1.0, 1.0);
    REQUIRE(ahead.led);
    CHECK(ahead.azimuthDeg == 14);
    CHECK(ahead.elevationDeg == 22);
    const auto east = leadSky(359, 30, 1, 30, 1.0, 1.0);
    REQUIRE(east.led);
    CHECK(east.azimuthDeg == 3);
    const auto west = leadSky(1, 30, 359, 30, 1.0, 1.0);
    REQUIRE(west.led);
    CHECK(west.azimuthDeg == 357);
    const auto clamped = leadSky(10, 20, 12, 20, 1.0, 10.0);
    CHECK(clamped.azimuthDeg == 14);
    const auto stale = leadSky(10, 20, 12, 20, 0.0, 1.0);
    CHECK_FALSE(stale.led);
    CHECK(stale.azimuthDeg == 12);
}

TEST_CASE("Horizontal high band commands 18 V and a LEO lead aims ahead of the last look") {
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost));
    QStringList commands;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                while (socket->canReadLine()) {
                    const auto cmd = socket->readLine();
                    commands << QString::fromLatin1(cmd).trimmed();
                    QByteArray reply;
                    if (cmd == "+p\n") reply = "get_pos:\nAzimuth: 180\nElevation: 45\nRPRT 0\n";
                    else if (cmd.startsWith("+P ")) reply = "set_pos: " + cmd.mid(3).trimmed() + "\nRPRT 0\n";
                    else if (cmd == "+S\n") reply = "stop:\nRPRT 0\n";
                    else FAIL("Unexpected command");
                    socket->write(reply);
                }
            });
        }
    });
    auto waitFor = [](const std::function<bool()>& ready) {
        QElapsedTimer timer;
        timer.start();
        while (!ready() && timer.elapsed() < 5000) {
            QApplication::processEvents();
            QThread::msleep(1);
        }
        return ready();
    };
    RotatorController rotor;
    rotor.connectTo("127.0.0.1", server.serverPort(), {});
    REQUIRE(waitFor([&] { return rotor.fresh(); }));
    REQUIRE(rotor.arm());
    PassChecklist check;
    check.leaseHeld = true;
    check.ifInSdrSpan = true;
    check.tleFreshOrNotRequired = true;
    check.biasCurrentOk = true;
    check.rotatorReadyOrOverride = true;
    check.powerConfirmed = true;
    StationProfile profile;
    profile.biasBackend = BiasBackend::External;
    profile.mission = StationMission::LeoTrack;
    profile.trueRfHz = 11.7e9;
    profile.horizontal = true;
    profile.highBand = true;
    profile.parkAz = 0;
    profile.parkEl = 0;
    std::string error;
    StationPassSession session(rotor);
    REQUIRE(session.arm(check, profile, 20, 30, &error));
    bool saw18 = false;
    for (const auto& row : session.metrics().records())
        if (row.name == "lnb.voltageV" && row.value == 18) saw18 = true;
    CHECK(saw18);
    session.noteSky(22, 30, 11.7e9, 0);
    QThread::msleep(250);
    QApplication::processEvents();
    session.noteSky(24, 30, 11.7e9, 0);
    const bool sawLead = waitFor([&] { return commands.contains("+P 26.000 30.000"); });
    std::string metricDump;
    for (const auto& row : session.metrics().records())
        if (row.name.find("lead") != std::string::npos || row.name == "track" || row.name.find("rotator.command") != std::string::npos)
            metricDump += row.name + "=" + std::to_string(row.value) + " " + row.text + "\n";
    INFO(commands.join(" | ").toStdString());
    INFO(metricDump);
    REQUIRE(sawLead);
    session.abort("test");
    REQUIRE(waitFor([&] { return !session.power().enabled(); }));
}

TEST_CASE("Sky feed accepts Doppler only for an armed in-mask fresh pass") {
    const auto rejected = skyFeedFromPass(false, true, 12, 30, 145.8e6, 100, 10, 10, 72);
    CHECK_FALSE(rejected.accepted);
    CHECK(rejected.reject == "No armed satellite pass");
    const auto low = skyFeedFromPass(true, true, 12, 4, 145.8e6, 100, 10, 10, 72);
    CHECK_FALSE(low.accepted);
    CHECK(low.reject == "Armed pass is below the elevation mask");
    const auto stale = skyFeedFromPass(true, true, 12, 30, 145.8e6, 100, 73 * 3600, 10, 72);
    CHECK_FALSE(stale.accepted);
    CHECK(stale.reject == "TLE is older than the station limit");
    const auto unknown = skyFeedFromPass(true, true, 12, 30, 145.8e6, 100, -1, 10, 72);
    CHECK(unknown.reject == "TLE age is unknown");
    const auto feed = skyFeedFromPass(true, true, 12.5, 31.0, 11.7e9, -2500, 3600, 10, 72);
    REQUIRE(feed.accepted);
    CHECK(feed.tleFresh);
    CHECK(feed.azimuthDeg == 12.5);
    CHECK(feed.elevationDeg == 31.0);
    CHECK(feed.trueRfHz == 11.7e9 - 2500);
    CHECK(feed.dopplerHz == -2500);
}

TEST_CASE("Pass folder stores the station profile and metric log") {
    StationProfile profile;
    profile.name = "pass-log";
    profile.trueRfHz = 11.7e9 - 2500;
    FrontEndMetrics metrics;
    metrics.add("sky.dopplerHz", -2500);
    const auto dir = std::filesystem::temp_directory_path() / "sdr-town-pass-log-test";
    std::filesystem::remove_all(dir);
    std::string error;
    REQUIRE(writePassFolder(dir.string(), profile, metrics, &error));
    std::string profileText;
    std::string metricText;
    {
        std::ifstream profileFile(dir / "station-profile.json");
        std::ifstream metricFile(dir / "metrics.jsonl");
        profileText.assign(std::istreambuf_iterator<char>(profileFile), {});
        metricText.assign(std::istreambuf_iterator<char>(metricFile), {});
    }
    CHECK(profileText.find("pass-log") != std::string::npos);
    CHECK(profileText.find("trueRfHz") != std::string::npos);
    CHECK(metricText.find("sky.dopplerHz") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("A selected radio queues the IF and a rejected tune turns Bias-T back off") {
    struct Reset {
        ~Reset() { setStationRadio(nullptr); }
    } reset;
    StationRadioRequest seen;
    int calls = 0;
    setStationRadio([&](const StationRadioRequest& request, std::string* error) {
        seen = request;
        ++calls;
        if (request.tune) {
            if (error) *error = "satcom lease is held";
            return false;
        }
        return true;
    });
    RotatorController rotor;
    StationPassSession session(rotor);
    PassChecklist check;
    check.leaseHeld = true;
    check.ifInSdrSpan = true;
    check.tleFreshOrNotRequired = true;
    check.biasCurrentOk = true;
    check.rotatorReadyOrOverride = true;
    check.powerConfirmed = true;
    StationProfile profile;
    profile.biasBackend = BiasBackend::SdrInternal;
    profile.mission = StationMission::Manual;
    profile.trueRfHz = 11.7e9;
    profile.radioIndex = 0;
    std::string error;
    CHECK_FALSE(session.arm(check, profile, 20, 30, &error));
    CHECK(error == "satcom lease is held");
    CHECK_FALSE(session.power().enabled());
    CHECK(seen.tune);
    CHECK(seen.ifHz > 1.94e9);
    CHECK(seen.ifHz < 1.96e9);
    CHECK(seen.voltageV == 13);
    CHECK(seen.biasBackend == BiasBackend::SdrInternal);
    CHECK(calls == 1);

    setStationRadio([&](const StationRadioRequest& request, std::string*) {
        seen = request;
        ++calls;
        return true;
    });
    profile.biasBackend = BiasBackend::External;
    profile.horizontal = true;
    profile.highBand = true;
    REQUIRE(session.arm(check, profile, 20, 30, &error));
    CHECK(seen.ifHz > 1.09e9);
    CHECK(seen.ifHz < 1.11e9);
    CHECK(seen.voltageV == 18);
    CHECK(seen.tone22kHz);
    CHECK(seen.biasBackend == BiasBackend::External);
    bool queued = false;
    for (const auto& row : session.metrics().records())
        if (row.name == "radio.tune" && row.text == "queued") queued = true;
    CHECK(queued);
    session.abort("test");
    CHECK(seen.releaseLease);
    CHECK_FALSE(seen.biasEnable);
}

TEST_CASE("Station panel and Tools rotator share one rotctld connection") {
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost));
    int sockets = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            ++sockets;
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                while (socket->canReadLine()) {
                    const auto cmd = socket->readLine();
                    QByteArray reply;
                    if (cmd == "+p\n") reply = "get_pos:\nAzimuth: 180\nElevation: 45\nRPRT 0\n";
                    else if (cmd.startsWith("+P ")) reply = "set_pos: " + cmd.mid(3).trimmed() + "\nRPRT 0\n";
                    else if (cmd == "+S\n") reply = "stop:\nRPRT 0\n";
                    else reply = "RPRT 0\n";
                    socket->write(reply);
                }
            });
        }
    });
    auto waitFor = [](const std::function<bool()>& ready) {
        QElapsedTimer timer;
        timer.start();
        while (!ready() && timer.elapsed() < 2500) {
            QApplication::processEvents();
            QThread::msleep(1);
        }
        return ready();
    };
    EquipmentWizard station;
    station.findChild<QSpinBox*>("rotatorPort")->setValue(server.serverPort());
    station.findChild<QPushButton*>("connectRotator")->click();
    REQUIRE(waitFor([&] { return sharedRotatorController().fresh(); }));
    CHECK(sockets == 1);
    AntennaControlWindow tools;
    tools.show();
    QApplication::processEvents();
    REQUIRE(waitFor([&] { return tools.findChild<QLabel*>("rotorPosition")->text().contains("180.0"); }));
    CHECK(sockets == 1);
    EquipmentWizard second;
    second.findChild<QPushButton*>("connectRotator")->click();
    CHECK(second.findChild<QLabel*>("skyFeed")->text().contains("Disconnect before changing"));
    CHECK(sockets == 1);
    sharedRotatorController().disconnectFromController();
    REQUIRE(waitFor([&] { return !sharedRotatorController().connected(); }));
}
