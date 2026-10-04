#include <QApplication>
#include <QWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkProxy>
#include <QEventLoop>
#include <QVariant>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslSocket>
#include <QTimer>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTemporaryFile>
#include <QDir>
#include <QFileInfo>
#include <iostream>
#include <windows.h>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QByteArray svg("<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32'>"
                         "<rect width='32' height='32' fill='#d62244'/></svg>");
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) return 1;
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    { QPainter painter(&image); renderer.render(&painter); }
    const QColor expected(0xd6, 0x22, 0x44);
    if (image.pixelColor(16, 16) != expected) return 2;
    const auto decoded = QImage::fromData(svg, "svg");
    if (decoded.isNull() || decoded.pixelColor(16, 16) != expected) return 3;
    QTemporaryFile file(QDir::tempPath() + "/sdr-qt-probe-XXXXXX.svg");
    if (!file.open() || file.write(svg) != svg.size() || !file.flush()) return 4;
    const auto icon = QIcon(file.fileName()).pixmap(32, 32).toImage();
    if (icon.isNull() || icon.pixelColor(icon.width() / 2, icon.height() / 2) != expected) return 5;
    QWidget widget;
    widget.resize(64, 64);
    widget.setStyleSheet("background: #d62244");
    if (widget.grab().toImage().pixelColor(32, 32) != expected) return 8;

    // Native TLS must still load, without contacting an external service.
    if (!QSslSocket::availableBackends().contains("schannel") ||
        !QSslSocket::setActiveBackend("schannel") || !QSslSocket::supportsSsl()) return 9;
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return 10;
    QObject::connect(&server, &QTcpServer::newConnection, [&server] {
        while (auto* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            // A complete request header is required before the single test reply.
            auto respond = [socket] {
                QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                if (request.size() > 8192) { socket->abort(); return; }
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("replied").toBool()) return;
                socket->setProperty("replied", true);
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 7\r\nConnection: close\r\n\r\nqt-test");
                socket->disconnectFromHost();
            };
            QObject::connect(socket, &QTcpSocket::readyRead, socket, respond);
            respond();
        }
    });
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    auto* reply = network.get(QNetworkRequest(QUrl(
        QString("http://127.0.0.1:%1/").arg(server.serverPort()))));
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    deadline.start(5000); // Bounded local QA, not a radio-path timer.
    if (!reply->isFinished()) loop.exec();
    if (!reply->isFinished() || reply->error() != QNetworkReply::NoError ||
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
        reply->readAll() != "qt-test") return 11;
    delete reply;
    // Confirm Windows actually loaded replacements from this disposable package.
    for (const auto* name : {L"Qt6Core.dll", L"Qt6Gui.dll", L"Qt6Widgets.dll", L"Qt6Network.dll",
                             L"Qt6Svg.dll", L"qsvg.dll", L"qsvgicon.dll", L"qwindows.dll",
                             L"qschannelbackend.dll"}) {
        wchar_t path[32768]{};
        HMODULE handle = GetModuleHandleW(name);
        const DWORD length = handle ? GetModuleFileNameW(handle, path, 32768) : 0;
        if (length == 0 || length >= 32768) return 6;
        const QString relative = QDir(QCoreApplication::applicationDirPath()).relativeFilePath(
            QFileInfo(QString::fromWCharArray(path)).canonicalFilePath());
        if (relative.startsWith("../") || QDir::isAbsolutePath(relative)) return 7;
        std::cout << "loaded replacement " << relative.toStdString() << '\n';
    }
    std::cout << "PASS Qt " << qVersion() << ": renderer/image/icon/widget pixels, loopback HTTP, native TLS\n";
    return 0;
}
