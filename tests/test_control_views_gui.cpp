#include "SatcomHubWidget.h"
#include "AircraftMapWidget.h"
#include "AdsBTrackStore.h"
#include "InmarsatWidget.h"
#include "InmarsatEngine.h"
#include "ControlMessageRange.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>

namespace {
struct OfflineViews {
    QNetworkProxy proxy = QNetworkProxy::applicationProxy();
    QVariant dryRun = qApp->property("sdrtown.guiDryRun");
    OfflineViews() {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 1));
        qApp->setProperty("sdrtown.guiDryRun", true);
    }
    ~OfflineViews() {
        QNetworkProxy::setApplicationProxy(proxy);
        qApp->setProperty("sdrtown.guiDryRun", dryRun);
    }
};
}

TEST_CASE("Control message ranges accept query strings and reject ambiguous values", "[aircraft][gui][control-views]") {
    int limit = 0, offset = 0;
    REQUIRE(controlMessageRange({}, limit, offset));
    CHECK(limit == 100); CHECK(offset == 0);
    REQUIRE(controlMessageRange({{"limit", "7"}, {"offset", "3"}}, limit, offset));
    CHECK(limit == 7); CHECK(offset == 3);
    REQUIRE(controlMessageRange({{"limit", 500}, {"offset", 100000}}, limit, offset));
    for (const QJsonValue value : {QJsonValue(-1), QJsonValue(0), QJsonValue(501),
            QJsonValue(1.5), QJsonValue(true), QJsonValue("bad"), QJsonValue(" 7"),
            QJsonValue("999999999999999999999"), QJsonValue(QJsonValue::Null)})
        CHECK_FALSE(controlMessageRange({{"limit", value}}, limit, offset));
}

TEST_CASE("Aircraft session catalog excludes tracks while selected views retain them", "[aircraft][gui][control-views]") {
    OfflineViews offline;
    SatcomHubWidget hub;
    auto* map = qobject_cast<AircraftMapWidget*>(hub.openReceiverSession("aircraft", "qa-catalog"));
    REQUIRE(map);
    for (uint32_t i = 0; i < 400; ++i)
        map->trackStore().ingestAdscPosition(0xA00000 + i, -34, 151, {}, "CATALOG");
    const auto catalog = hub.controlReceiverSessions("aircraft", "GET", {});
    REQUIRE(catalog.value("ok").toBool());
    CHECK(QJsonDocument(catalog).toJson(QJsonDocument::Compact).size() < 16384);
    bool found = false;
    for (const auto& value : catalog.value("sessions").toArray()) {
        const auto session = value.toObject();
        CHECK_FALSE(session.contains("tracks"));
        if (session.value("sessionId").toString() == "qa-catalog") found = true;
    }
    CHECK(found);
    const auto detail = hub.controlReceiverSessions("aircraft", "GET", {{"sessionId", "qa-catalog"}, {"view", "status"}});
    CHECK(detail.value("tracks").toArray().size() == 400);
    REQUIRE(hub.controlReceiverSessions("aircraft", "POST", {{"sessionId", "qa-catalog"}, {"action", "close"}}).value("ok").toBool());
}

TEST_CASE("Named Inmarsat message ranges select the requested source records", "[aircraft][gui][control-views]") {
    OfflineViews offline;
    SatcomHubWidget hub;
    auto* widget = hub.openInmarsatSession("qa-message-range");
    REQUIRE(widget);
    widget->engine().messageStore().clear();
    for (int i = 0; i < 12; ++i) {
        InmarsatMessage message;
        message.validated = true;
        message.text = "message-" + std::to_string(i);
        widget->engine().messageStore().push(message);
    }
    const auto result = hub.controlInmarsatSessions("GET", {{"sessionId", "qa-message-range"},
        {"view", "messages"}, {"limit", "2"}, {"offset", "3"}});
    REQUIRE(result.value("ok").toBool());
    const auto rows = result.value("messages").toArray();
    REQUIRE(rows.size() == 2);
    CHECK(rows.at(0).toObject().value("text").toString() == "message-8");
    CHECK(rows.at(1).toObject().value("text").toString() == "message-7");
    CHECK(hub.controlInmarsatSessions("GET", {{"sessionId", "qa-message-range"}, {"view", "messages"},
        {"limit", "2x"}}).value("status").toInt() == 400);
    REQUIRE(hub.controlInmarsatSessions("POST", {{"sessionId", "qa-message-range"}, {"action", "close"}}).value("ok").toBool());
}
