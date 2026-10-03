#include <QGuiApplication>
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
    QGuiApplication app(argc, argv);
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
    // Confirm Windows actually loaded replacements from this disposable package.
    for (const auto* name : {L"Qt6Svg.dll", L"qsvg.dll", L"qsvgicon.dll"}) {
        wchar_t path[32768]{};
        HMODULE handle = GetModuleHandleW(name);
        const DWORD length = handle ? GetModuleFileNameW(handle, path, 32768) : 0;
        if (length == 0 || length >= 32768) return 6;
        const QString relative = QDir(QCoreApplication::applicationDirPath()).relativeFilePath(
            QFileInfo(QString::fromWCharArray(path)).canonicalFilePath());
        if (relative.startsWith("../") || QDir::isAbsolutePath(relative)) return 7;
        std::cout << "loaded replacement " << relative.toStdString() << '\n';
    }
    std::cout << "PASS Qt " << qVersion() << ": renderer, image plugin, icon plugin pixels\n";
    return 0;
}
