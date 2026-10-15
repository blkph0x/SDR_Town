#include "SdrTownControlServer.h"
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QTcpSocket>
#include <QThread>
#include <stdexcept>
#include <memory>

#ifdef SDR_TOWN_CONTROL_TEST_MAIN
#include <catch2/catch_session.hpp>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
#endif

TEST_CASE("Control map pagination preserves one bounded immutable snapshot", "[control][paging]") {
    ControlSnapshotPager pager;
    QJsonArray aircraft, positions;
    for (int i = 0; i < 512; ++i) {
        QJsonObject row{{"id", i}, {"callsign", QString(700, 'X')}};
        aircraft.append(row);
        if (i < 256) positions.append(row);
    }
    int reads = 0;
    const auto now = ControlSnapshotPager::Clock::now();
    const auto produce = [&] { ++reads; return QJsonObject{{"ok", true}, {"aircraft", aircraft}, {"positions", positions}, {"revision", reads}}; };
    auto page = pager.respond("/v1/inmarsat/map-status", {}, produce(), now);
    const auto snapshot = page.value("page").toObject().value("snapshot").toString();
    REQUIRE_FALSE(snapshot.isEmpty());
    QJsonArray allAircraft, allPositions;
    int expectedOffset = 0;
    for (;;) {
        REQUIRE(page.value("ok").toBool());
        REQUIRE(QJsonDocument(page).toJson(QJsonDocument::Compact).size() <= ControlSnapshotPager::pageBytes);
        const auto meta = page.value("page").toObject();
        CHECK(meta.value("snapshot").toString() == snapshot);
        CHECK(meta.value("offset").toInt() == expectedOffset);
        CHECK(page.contains("revision") == (expectedOffset == 0));
        for (const auto& item : page.value("aircraft").toArray()) allAircraft.append(item);
        for (const auto& item : page.value("positions").toArray()) allPositions.append(item);
        if (meta.value("nextOffset").isNull()) break;
        expectedOffset = meta.value("nextOffset").toInt();
        page = pager.respond("/v1/inmarsat/map-status", {{"snapshot", snapshot}, {"offset", QString::number(expectedOffset)}}, {}, now);
    }
    CHECK(reads == 1);
    CHECK(allAircraft == aircraft); CHECK(allPositions == positions);
    CHECK(pager.respond("/v1/aircraft/map-status", {{"snapshot", snapshot}}, {}, now).value("status").toInt() == 409);
    CHECK(pager.respond("/v1/inmarsat/map-status", {{"snapshot", snapshot}, {"offset", "-1"}}, {}, now).value("status").toInt() == 400);
    CHECK(pager.respond("/v1/inmarsat/map-status", {{"snapshot", snapshot}}, {}, now + std::chrono::seconds(30)).value("status").toInt() == 409);
    CHECK(reads == 1);
}

TEST_CASE("Control pages reject oversized records and cannot cross named sessions", "[control][paging]") {
    ControlSnapshotPager pager;
    const auto now = ControlSnapshotPager::Clock::now();
    QJsonArray rows;
    for (int i = 0; i < 80; ++i) rows.append(QJsonObject{{"id", i}});
    auto produce = [&] { return QJsonObject{{"ok", true}, {"tracks", rows}}; };
    const auto first = pager.respond("/v1/inmarsat/sessions", {{"sessionId", "a"}, {"view", "map"}}, produce(), now);
    const auto id = first.value("page").toObject().value("snapshot").toString();
    REQUIRE_FALSE(id.isEmpty());
    CHECK(pager.respond("/v1/inmarsat/sessions", {{"sessionId", "b"}, {"view", "map"}, {"snapshot", id}}, {}, now).value("status").toInt() == 409);
    rows = QJsonArray{QJsonObject{{"large", QString(300000, 'X')}}};
    CHECK(pager.respond("/v1/aircraft/status", {}, produce(), now).value("status").toInt() == 413);
    rows = QJsonArray{QJsonObject{{"small", "one"}}};
    const auto small = pager.respond("/v1/aircraft/status", {}, produce(), now);
    CHECK_FALSE(small.contains("page"));
    CHECK(small.value("tracks").toArray() == rows);
    pager.clear();
    CHECK(pager.respond("/v1/inmarsat/sessions", {{"sessionId", "a"}, {"view", "map"}, {"snapshot", id}}, {}, now).value("status").toInt() == 409);
}

namespace {
bool until(const std::function<bool()>& ready) {
    QElapsedTimer clock; clock.start();
    while (!ready() && clock.elapsed() < 2000) {
        QCoreApplication::processEvents(); QThread::msleep(1);
    }
    return ready();
}
QByteArray exchange(SdrTownControlServer& server, const QByteArray& request) {
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.port());
    if (!until([&] { return socket.state() == QAbstractSocket::ConnectedState; })) return {};
    socket.write(request);
    QByteArray response;
    until([&] { response += socket.readAll(); return socket.state() == QAbstractSocket::UnconnectedState; });
    response += socket.readAll();
    return response;
}
QByteArray getRequest(const QByteArray& path) {
    return "GET " + path + " HTTP/1.1\r\nAuthorization: Bearer fixture\r\n\r\n";
}
QJsonObject responseBody(const QByteArray& response) {
    const auto separator = response.indexOf("\r\n\r\n");
    REQUIRE(separator >= 0);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(response.mid(separator + 4), &error);
    REQUIRE(error.error == QJsonParseError::NoError);
    REQUIRE(document.isObject());
    return document.object();
}
QJsonArray largeTracks() {
    QJsonArray tracks;
    for (int i = 0; i < 400; ++i) tracks.append(QJsonObject{{"id", i}, {"label", QString(700, 'X')}});
    return tracks;
}
}

TEST_CASE("Control named aircraft HTTP pages preserve metadata scope and one observation", "[control][paging]") {
    SdrTownControlServer server;
    SdrTownControlServer::Config cfg;
    cfg.port = 0; cfg.token = "fixture";
    REQUIRE(server.start(cfg));
    const auto tracks = largeTracks();
    int reads = 0;
    server.setRequestHandler([&](const auto& method, const auto& path, const auto& body) {
        ++reads;
        CHECK(method == "GET"); CHECK(path == "/v1/aircraft/sessions");
        CHECK(body.value("sessionId").toString() == "air-a");
        return QJsonObject{{"ok", true}, {"sessionId", "air-a"}, {"revision", reads}, {"tracks", tracks}};
    });
    const QByteArray route = "/v1/aircraft/sessions?sessionId=air-a&view=map";
    QByteArray next = route;
    QJsonArray reconstructed;
    QString snapshot;
    int offset = 0;
    for (int pages = 0; pages < 20; ++pages) {
        const auto wire = exchange(server, getRequest(next));
        REQUIRE(wire.startsWith("HTTP/1.1 200"));
        const auto body = responseBody(wire);
        REQUIRE(QJsonDocument(body).toJson(QJsonDocument::Compact).size() <= ControlSnapshotPager::pageBytes);
        REQUIRE(body.value("tracks").toArray().size() <= 64);
        CHECK(body.contains("revision") == (offset == 0));
        CHECK(body.contains("sessionId") == (offset == 0));
        const auto page = body.value("page").toObject();
        REQUIRE(page.value("total").toInt() == 400);
        CHECK(page.value("offset").toInt() == offset);
        if (snapshot.isEmpty()) snapshot = page.value("snapshot").toString();
        REQUIRE_FALSE(snapshot.isEmpty());
        CHECK(page.value("snapshot").toString() == snapshot);
        for (const auto& row : body.value("tracks").toArray()) reconstructed.append(row);
        if (page.value("nextOffset").isNull()) break;
        const int following = page.value("nextOffset").toInt();
        REQUIRE(following > offset);
        offset = following;
        next = route + "&snapshot=" + snapshot.toUtf8() + "&offset=" + QByteArray::number(offset);
    }
    CHECK(reconstructed == tracks);
    CHECK(reads == 1);
    for (const auto scope : {QByteArray("sessionId=air-b&view=map"), QByteArray("sessionId=air-a&view=status")}) {
        const auto wire = exchange(server, getRequest("/v1/aircraft/sessions?" + scope + "&snapshot=" + snapshot.toUtf8()));
        CHECK(wire.startsWith("HTTP/1.1 409"));
    }
    const auto unauthorized = exchange(server, "GET " + next + " HTTP/1.1\r\n\r\n");
    CHECK(unauthorized.startsWith("HTTP/1.1 401"));
    CHECK(reads == 1);
}

TEST_CASE("Control initial message selection and cached page offset are separate", "[control][paging]") {
    SdrTownControlServer server;
    SdrTownControlServer::Config cfg;
    cfg.port = 0; cfg.token = "fixture";
    REQUIRE(server.start(cfg));
    int reads = 0;
    server.setRequestHandler([&](const auto&, const auto& path, const auto& body) {
        ++reads;
        CHECK(path == "/v1/inmarsat/messages");
        CHECK(body.value("offset").toString() == "7");
        CHECK(body.value("limit").toString() == "100");
        QJsonArray rows;
        for (int i = 7; i < 107; ++i) rows.append(QJsonObject{{"id", i}});
        return QJsonObject{{"ok", true}, {"messages", rows}, {"sourceOffset", 7}};
    });
    const auto first = responseBody(exchange(server, getRequest("/v1/inmarsat/messages?offset=7&limit=100")));
    CHECK(first.value("messages").toArray().first().toObject().value("id").toInt() == 7);
    const auto page = first.value("page").toObject();
    CHECK(page.value("offset").toInt() == 0);
    REQUIRE(page.value("nextOffset").toInt() == 64);
    const auto second = responseBody(exchange(server, getRequest("/v1/inmarsat/messages?snapshot="
        + page.value("snapshot").toString().toUtf8() + "&offset=64")));
    CHECK(second.value("messages").toArray().first().toObject().value("id").toInt() == 71);
    CHECK(second.value("messages").toArray().size() == 36);
    CHECK_FALSE(second.contains("sourceOffset"));
    CHECK(reads == 1);
    for (const auto offset : {"-1", "1.5", "100001", "bogus", "%2B1", "99999999999999999999"}) {
        CHECK(exchange(server, getRequest(QByteArray("/v1/inmarsat/messages?offset=") + offset)).startsWith("HTTP/1.1 400"));
    }
    CHECK(reads == 1);
}

TEST_CASE("Control oversized successful POST acknowledges one mutation without replay", "[control][paging]") {
    SdrTownControlServer server;
    SdrTownControlServer::Config cfg;
    cfg.port = 0; cfg.token = "fixture";
    REQUIRE(server.start(cfg));
    int mutations = 0;
    const auto tracks = largeTracks();
    server.setRequestHandler([&](const auto& method, const auto&, const auto&) {
        CHECK(method == "POST");
        ++mutations;
        QCoreApplication::processEvents();
        return QJsonObject{{"ok", true}, {"tracks", tracks}};
    });
    for (const auto path : {"/v1/aircraft/map-control", "/v1/aircraft/sessions"}) {
        const QByteArray request = QByteArray("POST ") + path
            + " HTTP/1.1\r\nAuthorization: Bearer fixture\r\nContent-Length: 2\r\n\r\n{}";
        const int before = mutations;
        const auto wire = exchange(server, request + request);
        REQUIRE(wire.startsWith("HTTP/1.1 200"));
        CHECK(wire.size() < 1024);
        const auto body = responseBody(wire);
        CHECK(body.value("ok").toBool()); CHECK(body.value("stateOmitted").toBool());
        CHECK_FALSE(body.contains("tracks"));
        CHECK(mutations == before + 1);
    }
    server.setRequestHandler([](const auto&, const auto&, const auto&) {
        return QJsonObject{{"ok", false}, {"status", 409}, {"error", "rejected"}};
    });
    CHECK(exchange(server, "POST /v1/aircraft/map-control HTTP/1.1\r\nAuthorization: Bearer fixture\r\n\r\n").startsWith("HTTP/1.1 409"));
}

TEST_CASE("Control paged handler can destroy its server or restart without publishing stale work", "[control][paging]") {
    auto server = std::make_unique<SdrTownControlServer>();
    SdrTownControlServer::Config cfg;
    cfg.port = 0; cfg.token = "fixture";
    REQUIRE(server->start(cfg));
    const QJsonObject document{{"ok", true}, {"tracks", largeTracks()}};
    bool throws = false;
    bool restart = false;
    SECTION("destroy and return") {}
    SECTION("destroy and throw") { throws = true; }
    SECTION("stop restart and return") { restart = true; }
    SECTION("stop restart and throw") { restart = true; throws = true; }
    int calls = 0;
    server->setRequestHandler([&](const auto&, const auto&, const auto&) -> QJsonObject {
        ++calls;
        const auto result = document;
        if (restart) {
            server->stop();
            REQUIRE(server->start(cfg));
        } else server.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (throws) throw std::runtime_error("cancelled");
        return result;
    });
    CHECK(exchange(*server, getRequest("/v1/aircraft/map-status")).isEmpty());
    CHECK(calls == 1);
    if (restart) {
        server->setRequestHandler([](const auto&, const auto&, const auto&) { return QJsonObject{{"ok", true}, {"fresh", true}}; });
        const auto body = responseBody(exchange(*server, getRequest("/v1/aircraft/map-status")));
        CHECK(body.value("fresh").toBool());
        CHECK_FALSE(body.contains("page"));
    } else CHECK_FALSE(server);
}

TEST_CASE("Control snapshot scope cannot collide through embedded separators", "[control][paging]") {
    ControlSnapshotPager pager;
    const QJsonObject scope{{"sessionId", "a\nb"}, {"view", "c"}};
    const auto first = pager.respond("/route", scope, {{"ok", true}, {"tracks", largeTracks()}});
    const auto id = first.value("page").toObject().value("snapshot");
    CHECK(pager.respond("/route", {{"sessionId", "a"}, {"view", "b\nc"}, {"snapshot", id}}).value("status").toInt() == 409);
    CHECK(pager.respond("/route", {{"sessionId", 1}, {"snapshot", id}}).value("status").toInt() == 400);
}

TEST_CASE("Control stop closes already accepted clients", "[control]") {
    SdrTownControlServer server; SdrTownControlServer::Config cfg;
    cfg.port = 0; cfg.token = "fixture"; cfg.allowUnauthenticated = false;
    REQUIRE(server.start(cfg));
    int commands = 0;
    server.setRequestHandler([&](const auto&, const auto&, const auto&) { ++commands; return QJsonObject{{"ok", true}}; });
    QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost, server.port());
    REQUIRE(until([&] { return socket.state() == QAbstractSocket::ConnectedState; }));
    socket.write("POST /v1/tune HTTP/1.1\r\nAuthorization: Bearer fixture\r\n");
    REQUIRE(until([&] { return !server.findChildren<QTcpSocket*>().isEmpty(); }));
    server.stop();
    CHECK(until([&] { return socket.state() == QAbstractSocket::UnconnectedState; }));
    CHECK(commands == 0);
}

TEST_CASE("Control rejects ambiguous framing and does not expose exceptions", "[control]") {
    SdrTownControlServer server; SdrTownControlServer::Config cfg;
    cfg.port = 0; cfg.token = "fixture"; cfg.allowUnauthenticated = false;
    REQUIRE(server.start(cfg));
    int commands = 0;
    server.setRequestHandler([&](const auto&, const auto&, const auto&) { ++commands; return QJsonObject{{"ok", true}}; });
    for (const QByteArray headers : {QByteArray("Content-Length: -1\r\n"),
        QByteArray("Content-Length: rubbish\r\n"),
        QByteArray("Content-Length: 0\r\nContent-Length: 2\r\n"),
        QByteArray("Transfer-Encoding: chunked\r\n")}) {
        INFO(headers.constData());
        auto response = exchange(server, "POST /v1/tune HTTP/1.1\r\nAuthorization: Bearer fixture\r\n" + headers + "\r\n{}");
        CHECK(response.startsWith("HTTP/1.1 400"));
    }
    CHECK(commands == 0);
    auto unauthorized = exchange(server, "POST /v1/tune HTTP/1.1\r\nContent-Length: 1\r\n\r\n[");
    CHECK(unauthorized.startsWith("HTTP/1.1 401"));
    server.setRequestHandler([](const auto&, const auto&, const auto&) -> QJsonObject {
        throw std::runtime_error("private-key-path-do-not-return");
    });
    const auto failed = exchange(server, "POST /v1/tune HTTP/1.1\r\nAuthorization: Bearer fixture\r\n\r\n");
    CHECK(failed.startsWith("HTTP/1.1 500"));
    CHECK_FALSE(failed.contains("private-key-path"));
}

TEST_CASE("Control bounds idle clients and dispatches a valid request only once", "[control]") {
    SdrTownControlServer server; SdrTownControlServer::Config cfg;
    cfg.port = 0; cfg.token = "fixture"; cfg.allowUnauthenticated = false;
    cfg.maxConnections = 1; cfg.requestTimeoutMs = 100;
    REQUIRE(server.start(cfg));
    QTcpSocket idle; idle.connectToHost(QHostAddress::LocalHost, server.port());
    REQUIRE(until([&] { return server.statistics()["activeConnections"].toInt() == 1; }));
    QTcpSocket excess; excess.connectToHost(QHostAddress::LocalHost, server.port());
    REQUIRE(until([&] { return server.statistics()["rejected"].toInt() == 1; }));
    REQUIRE(until([&] { return idle.state() == QAbstractSocket::UnconnectedState; }));
    CHECK(server.statistics()["timeouts"].toInt() == 1);
    int commands = 0;
    server.setRequestHandler([&](const auto&, const auto&, const auto& body) {
        ++commands; CHECK(body["frequency"].toInt() == 123); return QJsonObject{{"ok", true}};
    });
    const QByteArray request = "POST /v1/tune HTTP/1.1\r\nAuthorization: Bearer fixture\r\nContent-Length: 17\r\n\r\n{\"frequency\":123}";
    auto response = exchange(server, request + request);
    CHECK(response.startsWith("HTTP/1.1 200")); CHECK(commands == 1);
    CHECK(server.statistics()["dispatched"].toInt() == 1);
    response = exchange(server, "POST /v1/tune HTTP/1.1\r\nContent-Length: 999999999\r\n\r\n");
    CHECK(response.startsWith("HTTP/1.1 413")); CHECK(commands == 1);
    server.stop(); CHECK(server.statistics()["activeConnections"].toInt() == 0);
    REQUIRE(server.start(cfg));
    response = exchange(server, request);
    CHECK(response.startsWith("HTTP/1.1 200")); CHECK(commands == 2);
}

TEST_CASE("Control handler cancellation remains safe when it throws", "[control]") {
    SdrTownControlServer server; SdrTownControlServer::Config cfg;
    cfg.port = 0; cfg.token = "fixture"; cfg.allowUnauthenticated = false;
    REQUIRE(server.start(cfg));
    server.setRequestHandler([&](const auto&, const auto&, const auto&) -> QJsonObject {
        server.stop();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        throw std::runtime_error("retired connection");
    });
    CHECK(exchange(server, "GET /v1/state HTTP/1.1\r\nAuthorization: Bearer fixture\r\n\r\n").isEmpty());
    CHECK(server.statistics()["activeConnections"].toInt() == 0);
    CHECK(server.statistics()["handlerErrors"].toInt() == 1);
}

TEST_CASE("Control handlers run after native read notification observers", "[control]") {
    SdrTownControlServer server; SdrTownControlServer::Config cfg;
    cfg.port = 0; cfg.token = "fixture"; cfg.allowUnauthenticated = false;
    REQUIRE(server.start(cfg));
    bool observed = false;
    int commands = 0;
    server.setRequestHandler([&](const auto&, const auto&, const auto&) {
        CHECK(observed);
        ++commands;
        return QJsonObject{{"ok", true}};
    });
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.port());
    REQUIRE(until([&] { return !server.findChildren<QTcpSocket*>().isEmpty(); }));
    auto* accepted = server.findChildren<QTcpSocket*>().front();
    QObject::connect(accepted, &QTcpSocket::readyRead, &server,
                     [&] { observed = true; }, Qt::DirectConnection);
    socket.write("GET /v1/state HTTP/1.1\r\nAuthorization: Bearer fixture\r\n\r\n");
    REQUIRE(until([&] { return commands == 1; }));
    CHECK(observed);
}

TEST_CASE("Control queued requests cannot survive connection retirement", "[control]") {
    SdrTownControlServer server; SdrTownControlServer::Config cfg;
    cfg.port = 0; cfg.token = "fixture"; cfg.allowUnauthenticated = false;
    REQUIRE(server.start(cfg));
    int commands = 0;
    server.setRequestHandler([&](const auto&, const auto&, const auto&) {
        ++commands; return QJsonObject{{"ok", true}};
    });
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.port());
    REQUIRE(until([&] { return !server.findChildren<QTcpSocket*>().isEmpty(); }));
    auto* accepted = server.findChildren<QTcpSocket*>().front();
    // Buffer a real request and queue dispatch without pumping posted events.
    socket.write("GET /v1/state HTTP/1.1\r\nAuthorization: Bearer fixture\r\n\r\n");
    REQUIRE(socket.flush());
    REQUIRE(accepted->waitForReadyRead(2000));
    REQUIRE(accepted->bytesAvailable() > 0);
    CHECK(commands == 0);
    server.stop();
    REQUIRE(server.start(cfg));
    SECTION("retired socket still awaiting deletion") {
        QCoreApplication::sendPostedEvents(&server, QEvent::MetaCall);
    }
    SECTION("retired socket already deleted") {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::sendPostedEvents(&server, QEvent::MetaCall);
    }
    CHECK(commands == 0);
    const auto response = exchange(server, "GET /v1/state HTTP/1.1\r\nAuthorization: Bearer fixture\r\n\r\n");
    CHECK(response.startsWith("HTTP/1.1 200"));
    CHECK(commands == 1);
}

TEST_CASE("DEC-0206 default Config refuses start without a token", "[control]") {
    SdrTownControlServer::Config cfg;
    CHECK(cfg.allowUnauthenticated == false);
    CHECK(cfg.token.isEmpty());
    SdrTownControlServer server;
    QString error;
    CHECK_FALSE(server.start(cfg, &error));
    CHECK(error.contains("token"));
    CHECK_FALSE(server.running());

    cfg.token = QStringLiteral("   ");
    CHECK_FALSE(server.start(cfg, &error));
    CHECK_FALSE(server.running());
}

TEST_CASE("DEC-0206 empty token is 401; health stays open", "[control]") {
    SdrTownControlServer server;
    SdrTownControlServer::Config cfg;
    cfg.port = 0;
    cfg.token = QStringLiteral("fixture");
    cfg.allowUnauthenticated = false;
    REQUIRE(server.start(cfg));
    int commands = 0;
    server.setRequestHandler([&](const auto&, const auto&, const auto&) {
        ++commands;
        return QJsonObject{{"ok", true}};
    });

    const auto health = exchange(server, "GET /v1/health HTTP/1.1\r\n\r\n");
    CHECK(health.startsWith("HTTP/1.1 200"));

    const auto status = exchange(server, "GET /v1/status HTTP/1.1\r\n\r\n");
    CHECK(status.startsWith("HTTP/1.1 401"));

    const auto missing = exchange(server, "POST /v1/tune HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}");
    CHECK(missing.startsWith("HTTP/1.1 401"));

    const auto wrong = exchange(server,
        "POST /v1/tune HTTP/1.1\r\nAuthorization: Bearer wrong-token\r\nContent-Length: 2\r\n\r\n{}");
    CHECK(wrong.startsWith("HTTP/1.1 401"));

    const auto headerTok = exchange(server,
        "POST /v1/tune HTTP/1.1\r\nx-sdrtown-token: fixture\r\nContent-Length: 2\r\n\r\n{}");
    CHECK(headerTok.startsWith("HTTP/1.1 200"));

    // Health skips auth but still reaches the handler; 401s must not.
    CHECK(commands == 2);
}

TEST_CASE("DEC-0206 explicit allowUnauthenticated still starts without a token", "[control]") {
    SdrTownControlServer server;
    SdrTownControlServer::Config cfg;
    cfg.port = 0;
    cfg.allowUnauthenticated = true;
    REQUIRE(server.start(cfg));
    int commands = 0;
    server.setRequestHandler([&](const auto&, const auto&, const auto&) {
        ++commands;
        return QJsonObject{{"ok", true}};
    });
    const auto response = exchange(server, "GET /v1/status HTTP/1.1\r\n\r\n");
    CHECK(response.startsWith("HTTP/1.1 200"));
    CHECK(commands == 1);
}
