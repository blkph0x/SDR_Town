#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>
#include <functional>
#include <cstdint>
#include <vector>

using SstvPreview = std::function<void(const QImage&, const QString&, int)>;

// DEC-0094: bounded helper JSONL parser; no radio/UI ownership.
class SstvProgress final {
public:
    using Complete = std::function<void(const QImage&,const QByteArray&)>;
    explicit SstvProgress(SstvPreview preview,Complete complete={}):preview_(std::move(preview)),complete_(std::move(complete)) {}
    void append(const QByteArray& bytes);
    void finish();
    const QByteArray& metadata() const { return metadata_; }
    const std::vector<QImage>& images() const { return images_; }
private:
    void line(const QByteArray& bytes);
    SstvPreview preview_;
    Complete complete_;
    uint64_t count_=0;
    QByteArray pending_,metadata_;
    size_t bytes_=0;
    QImage image_;
    QString mode_;
    int rows_=0;
    std::vector<bool> seen_;
    std::vector<QImage> images_;
};
