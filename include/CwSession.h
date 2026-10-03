#pragma once
#include "CwDecoder.h"
#include <QString>
#include <functional>

struct CwProgress {
    CwSnapshot decoder;
    QString source, status;
    uint64_t iqGaps = 0;
};
using CwCancel = std::function<bool()>;
using CwPublish = std::function<void(const CwProgress&)>;
using CwRun = std::function<void(CwOptions, const CwCancel&, const CwPublish&)>;

void decodeCwFile(const QString& path, CwOptions options, const CwCancel& cancel, const CwPublish& publish);
