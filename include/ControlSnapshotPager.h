#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <chrono>
#include <algorithm>
#include <map>

// GUI/control-server thread only. Pages are from one immutable observation,
// not successive live reports that can skip or duplicate moving aircraft.
class ControlSnapshotPager {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr qsizetype pageBytes = 192 * 1024;
    static constexpr qsizetype cacheBytes = 8 * 1024 * 1024;
    void clear() { snapshots_.clear(); bytes_ = 0; }
    static QJsonObject validateRequest(const QJsonObject& request) {
        for (const auto* name : {"sessionId", "view"}) {
            if (request.contains(name) && !request.value(name).isString())
                return error(400, "Invalid snapshot scope");
        }
        if (request.contains("snapshot")) {
            const auto id = request.value("snapshot");
            if (!id.isString() || id.toString().isEmpty() || id.toString().size() > 64)
                return error(400, "Invalid snapshot");
        }
        if (request.contains("offset")) {
            const auto value = request.value("offset");
            bool ok = false;
            if (value.isString()) {
                const auto text = value.toString();
                const int n = text.toInt(&ok);
                ok = ok && n >= 0 && n <= 100000 && !text.isEmpty()
                    && std::all_of(text.begin(), text.end(), [](QChar c) { return c >= QChar('0') && c <= QChar('9'); });
            } else if (value.isDouble()) {
                const double n = value.toDouble();
                ok = n >= 0 && n <= 100000 && n == int(n);
            }
            if (!ok) return error(400, "Invalid page offset");
        }
        return {};
    }
    // Production occurs outside this object: application handlers can destroy
    // its owner or stop/restart the server while processing nested Qt events.
    QJsonObject respond(const QString& route, const QJsonObject& request,
                        QJsonObject document = {},
                        Clock::time_point now = Clock::now()) {
        const auto invalid = validateRequest(request);
        if (!invalid.isEmpty()) return invalid;
        for (auto it = snapshots_.begin(); it != snapshots_.end();) {
            if (now - it->second.created >= std::chrono::seconds(30)) {
                bytes_ -= it->second.bytes; it = snapshots_.erase(it);
            } else ++it;
        }
        const auto key = QJsonDocument(QJsonArray{route, request.value("sessionId").toString(),
            request.value("view").toString()}).toJson(QJsonDocument::Compact);
        QString id = request.value("snapshot").toString();
        int offset = request.value("offset").isString()
            ? request.value("offset").toString().toInt() : request.value("offset").toInt();
        if (id.isEmpty()) {
            // Existing message offsets belong to the source until pagination starts.
            offset = 0;
            if (document.value("ok").isBool() && !document.value("ok").toBool()) return document;
            int total = 0;
            for (const auto* name : arrays) total = std::max(total, int(document.value(name).toArray().size()));
            const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Compact).size();
            if (total <= 64 && bytes <= pageBytes) return document;
            if (bytes > cacheBytes) return error(413, "Control snapshot exceeds the cache limit");
            while (!snapshots_.empty() && (snapshots_.size() >= 32 || bytes_ + bytes > cacheBytes)) {
                auto oldest = std::min_element(snapshots_.begin(), snapshots_.end(), [](const auto& a, const auto& b) {
                    return a.second.created < b.second.created;
                });
                bytes_ -= oldest->second.bytes; snapshots_.erase(oldest);
            }
            id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            snapshots_.emplace(id, Snapshot{key, std::move(document), now, bytes, total});
            bytes_ += bytes;
        }
        const auto it = snapshots_.find(id);
        if (it == snapshots_.end() || it->second.key != key) return error(409, "Snapshot expired; restart from the first page");
        const auto& snapshot = it->second;
        if (offset > snapshot.total) return error(400, "Page offset exceeds snapshot length");
        int count = std::min(64, snapshot.total - offset);
        for (;;) {
            QJsonObject result = offset == 0 ? snapshot.document : QJsonObject{{"ok", true}};
            for (const auto* name : arrays) {
                if (!snapshot.document.value(name).isArray()) continue;
                const auto source = snapshot.document.value(name).toArray();
                QJsonArray page;
                for (int i = offset; i < std::min(offset + count, int(source.size())); ++i) page.append(source[i]);
                result.insert(name, page);
            }
            const int next = offset + count;
            result.insert("page", QJsonObject{{"snapshot", id}, {"offset", offset},
                {"nextOffset", next < snapshot.total ? QJsonValue(next) : QJsonValue(QJsonValue::Null)},
                {"total", snapshot.total}});
            if (QJsonDocument(result).toJson(QJsonDocument::Compact).size() <= pageBytes) return result;
            if (count <= 1) return error(413, "Control snapshot metadata or record exceeds the page limit");
            count = std::max(1, count / 2);
        }
    }
private:
    static QJsonObject error(int status, const char* message) {
        return {{"ok", false}, {"status", status}, {"error", message}};
    }
    static constexpr const char* arrays[] = {"aircraft", "positions", "tracks", "messages"};
    struct Snapshot { QByteArray key; QJsonObject document; Clock::time_point created; qsizetype bytes; int total; };
    std::map<QString, Snapshot> snapshots_;
    qsizetype bytes_ = 0;
};
