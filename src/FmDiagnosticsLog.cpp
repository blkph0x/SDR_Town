#include "FmDiagnosticsLog.h"
#include "RemoteDiagnostics.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVariant>

FmDiagnosticsLog::FmDiagnosticsLog(QString directory) {
    if(directory.isEmpty()) directory=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/fm_diagnostics";
    QDir().mkpath(directory);
    path_=QDir(directory).filePath("fm-"+QString::number(QCoreApplication::applicationPid())+".jsonl");
    lock_=std::make_unique<QLockFile>(path_+".lock");
    lock_->tryLock(0);
    // Retain at most eight completed process logs; never remove an active writer.
    int retained=0;
    for(const auto& entry:QDir(directory).entryInfoList({"fm-*.jsonl"},QDir::Files,QDir::Time)) {
        if(entry.absoluteFilePath()==path_)continue;
        QLockFile candidate(entry.absoluteFilePath()+".lock");
        if(!candidate.tryLock(0))continue;
        if(++retained>8) {
            QFile::remove(entry.absoluteFilePath());
            QFile::remove(entry.absoluteFilePath()+".1");
        }
    }
}
QJsonObject FmDiagnosticsLog::sample() {
    static constexpr const char* names[]{"blocks","inputSamples","discriminatorSamples","audioSamples",
        "resets","emptyAudioBlocks","resamplerLookaheadReads","resamplerPhaseRepairs",
        "processingUs","inputUs","overBudgetBlocks","channelizerUs","discriminatorAndLpfUs",
        "resamplerUs","postAudioUs","requestedAudioSamples","hintMismatchBlocks",
        "maxBlockUs","maxFirDelayUs","maxPcmDelayUs"};
    static_assert(std::size(names)==fmDiagnostics::Count);
    QJsonObject modes;
    for(int mode=0;mode<2;++mode) {
        const auto s=fmDiagnostics::snapshot(mode!=0);
        if(s[fmDiagnostics::Blocks]==lastBlocks_[mode])continue;
        lastBlocks_[mode]=s[fmDiagnostics::Blocks];
        QJsonObject counters;
        for(size_t i=0;i<s.size();++i)counters[names[i]]=double(s[i]);
        modes[mode ? "wfm" : "nfm"]=counters;
    }
    if(modes.isEmpty())return {};
    QJsonObject report{{"schema",1},{"scope","process-mode-approximate-cumulative"},
        {"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"modes",modes}};
    // DEC-0142: <=2 MiB per running instance, only numeric aggregates. Never
    // serialize samples, tuning frequencies, station IDs or receiver identity.
    const auto line=QJsonDocument(report).toJson(QJsonDocument::Compact)+'\n';
    QFile file(path_);
    bool rotated=lock_->isLocked();
    if(rotated && file.size()+line.size()>1024*1024) {
        const QString backup=path_+".1";
        rotated=(!QFile::exists(backup)||QFile::remove(backup)) && file.rename(backup);
        file.setFileName(path_);
    }
    if(!lock_->isLocked() || !rotated || !file.open(QIODevice::WriteOnly|QIODevice::Append) || file.write(line)!=line.size())
        report["localWriteFailed"]=true;
    return report;
}
namespace {
class FmDiagnosticsThread final : public QThread {
public:
    explicit FmDiagnosticsThread(QObject* parent):QThread(parent) {}
    ~FmDiagnosticsThread() override { quit(); wait(); }
    void run() override {
        FmDiagnosticsLog log;
        const auto publish=[&] {
            const auto report=log.sample();
            if(!report.isEmpty())remoteDiagnosticsSubmit("fm.pipeline.sample",
                report.contains("localWriteFailed")?"warn":"info",report);
        };
        QTimer timer;
        connect(&timer,&QTimer::timeout,&timer,publish);
        timer.start(30000); // Diagnostic sampling policy, not RF timing.
        exec();
        publish();
    }
};
}
void startFmDiagnostics(QObject* parent) {
    if(!parent || parent->property("fmDiagnosticsStarted").toBool())return;
    parent->setProperty("fmDiagnosticsStarted",true);
    auto* worker=new FmDiagnosticsThread(parent);
    worker->setObjectName("FM diagnostics");
    worker->start();
}
