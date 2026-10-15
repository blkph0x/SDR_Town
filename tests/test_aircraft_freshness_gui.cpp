#include "AircraftMapWidget.h"
#include "AdsBTrackStore.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QJsonArray>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkProxy>
#include <QSettings>

namespace {
struct AircraftGuiIsolation {
    QNetworkProxy previous = QNetworkProxy::applicationProxy();
    QSettings settings{"SDR_Town", "SDR Town"};
    QString key = "aircraft/sessions/qa-freshness-gui/networkEnabled";
    QVariant saved = settings.value(key);
    AircraftGuiIsolation() {
        // No real tiles, aircraft lookups or radio actions in this UI gate.
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 1));
        settings.setValue(key, false);
    }
    ~AircraftGuiIsolation() {
        QNetworkProxy::setApplicationProxy(previous);
        if (saved.isValid()) settings.setValue(key, saved); else settings.remove(key);
    }
};

void saveAircraftVisual(QWidget& widget, const char* name) {
    const auto directory = qEnvironmentVariable("SDR_TOWN_TEST_VISUAL_DIR");
    if (directory.isEmpty()) return;
    REQUIRE(QDir().mkpath(directory));
    CHECK(widget.grab().save(directory + "/" + name + ".png"));
}
}

TEST_CASE("Aircraft native map hides stale network coordinates and renders their real source", "[aircraft][gui][freshness]") {
    AircraftGuiIsolation isolation;
    AircraftMapWidget map(nullptr, "qa-freshness-gui");
    map.resize(1100, 650); map.show(); QApplication::processEvents();
    auto& store = map.trackStore();
    double lat, lon; store.observer(&lat, &lon, nullptr);
    const auto now = QDateTime::currentSecsSinceEpoch();
    auto row = nlohmann::json::array({"40621d", "TEST", "", now - 121, now,
        lon, lat, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr});
    store.setNetworkEnabled(true);
    REQUIRE(store.mergeNetworkJson(nlohmann::json{{"states", {row}}}.dump(), store.networkGeneration()));
    map.refreshUi(); QApplication::processEvents();
    CHECK(map.webStatus().value("tracks").toArray().isEmpty());
    CHECK(map.grab().toImage().pixelColor(map.width() / 2, map.height() / 2) != QColor(80, 160, 255));
    row[3] = now;
    REQUIRE(store.mergeNetworkJson(nlohmann::json{{"states", {row}}}.dump(), store.networkGeneration()));
    map.refreshUi(); QApplication::processEvents();
    REQUIRE(map.webStatus().value("tracks").toArray().size() == 1);
    CHECK(map.grab().toImage().pixelColor(map.width() / 2, map.height() / 2) == QColor(80, 160, 255));
    saveAircraftVisual(map, "aircraft-fresh-network");
    store.setNetworkEnabled(false);
    map.refreshUi(); QApplication::processEvents();
    CHECK(map.webStatus().value("tracks").toArray().isEmpty());
    CHECK(map.grab().toImage().pixelColor(map.width() / 2, map.height() / 2) != QColor(80, 160, 255));
    map.hide();
}

TEST_CASE("Aircraft native details explicitly label missing altitude and speed", "[aircraft][gui][freshness]") {
    AircraftGuiIsolation isolation;
    AircraftMapWidget map(nullptr, "qa-freshness-gui");
    map.resize(1100, 650);
    double lat, lon; map.trackStore().observer(&lat, &lon, nullptr);
    map.trackStore().ingestAdscPosition(0x40621D, lat, lon, {}, "UNKNOWN");
    map.show(); map.refreshUi(); QApplication::processEvents();
    const auto webTrack = map.webStatus().value("tracks").toArray().at(0).toObject();
    CHECK(webTrack.value("altFt").isNull());
    CHECK(webTrack.value("gsKt").isNull());
    CHECK(map.grab().toImage().pixelColor(map.width() / 2, map.height() / 2) == QColor(255, 180, 40));
    const QPointF point(map.width() / 2.0, map.height() / 2.0);
    QMouseEvent click(QEvent::MouseButtonPress, point, map.mapToGlobal(point.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&map, &click); QApplication::processEvents();
    auto* dialog = map.findChild<QDialog*>(); REQUIRE(dialog);
    auto* label = dialog->findChild<QLabel*>(); REQUIRE(label);
    INFO(label->text().toStdString());
    CHECK_FALSE(label->text().contains("nan", Qt::CaseInsensitive));
    CHECK(label->text().contains("Alt unknown", Qt::CaseInsensitive));
    CHECK(label->text().contains("Speed unknown", Qt::CaseInsensitive));
    saveAircraftVisual(*dialog, "aircraft-missing-details");
    dialog->close(); map.hide();
}

TEST_CASE("Aircraft native markers use only valid headings and preserve measured zero values", "[aircraft][gui][freshness]") {
    AircraftGuiIsolation isolation;
    AircraftMapWidget map(nullptr, "qa-freshness-gui");
    map.resize(1100, 650); map.show(); QApplication::processEvents();
    auto& store = map.trackStore();
    double lat, lon; store.observer(&lat, &lon, nullptr);
    const auto now = QDateTime::currentSecsSinceEpoch();
    auto row = nlohmann::json::array({"40621d", "TEST", "", now, now,
        lon, lat, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr});
    store.setNetworkEnabled(true);
    const int x = map.width() / 2, y = map.height() / 2;
    for (const auto heading : nlohmann::json::array({nullptr, 0, 90, -1, 360, "90"})) {
        row[10] = heading;
        REQUIRE(store.mergeNetworkJson(nlohmann::json{{"states", {row}}}.dump(), store.networkGeneration()));
        map.refreshUi(); QApplication::processEvents();
        const auto picture = map.grab().toImage();
        // The narrow arrow tip is black outline; a directionless dot stops at 5 px.
        CHECK((picture.pixelColor(x, y - 7) == QColor(Qt::black)) == (heading == 0));
        CHECK((picture.pixelColor(x + 7, y) == QColor(Qt::black)) == (heading == 90));
        CHECK_FALSE(store.trackByIcao(0x40621D).groundSpeedValid);
        if (heading == 0) saveAircraftVisual(map, "aircraft-heading-north");
        if (heading == 90) saveAircraftVisual(map, "aircraft-heading-east");
    }
    row[7] = row[9] = row[10] = 0;
    REQUIRE(store.mergeNetworkJson(nlohmann::json{{"states", {row}}}.dump(), store.networkGeneration()));
    map.refreshUi(); QApplication::processEvents();
    const QPointF point(x, y);
    QMouseEvent click(QEvent::MouseButtonPress, point, map.mapToGlobal(point.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&map, &click); QApplication::processEvents();
    auto* dialog = map.findChild<QDialog*>(); REQUIRE(dialog);
    auto* label = dialog->findChild<QLabel*>(); REQUIRE(label);
    CHECK(label->text().contains("Alt 0 ft"));
    CHECK(label->text().contains("Speed 0 kt"));
    CHECK_FALSE(label->text().contains("unknown", Qt::CaseInsensitive));
    saveAircraftVisual(*dialog, "aircraft-zero-details");
    dialog->close(); map.hide();
}
