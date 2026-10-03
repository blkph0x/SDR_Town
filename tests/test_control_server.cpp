#include "SdrTownControlServer.h"
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QTcpSocket>
#include <QThread>
#include <stdexcept>

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
