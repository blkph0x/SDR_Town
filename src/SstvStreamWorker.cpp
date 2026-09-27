#include "SstvStreamWorker.h"
#include "SstvRateConverter.h"
#include "SstvModes.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QDateTime>
#include <spdlog/spdlog.h>
#include <QThread>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
constexpr qint64 maxPipeBytes=128*1024;
constexpr qint64 maxBlockBytes=(SstvInputEvent::maxSamples+1)*6*2;
}

SstvStreamResult decodeSstvStream(const SstvStreamRead& next,const QString& mode,
                                 const std::function<bool()>& cancelled,const SstvPreview& preview,const QString& archiveDirectory) {
    require(QCoreApplication::instance()!=nullptr,"SSTV worker needs an application instance");
    require(QThread::currentThread()!=QCoreApplication::instance()->thread(),"SSTV streaming must run off the GUI thread");
    require(bool(next),"SSTV stream source is missing");
    require(sstvStreamingModeOk(mode.toStdString()),"Unsupported SSTV streaming mode (digital prototype is file-only)");
    const auto cancel=[&]{require(!cancelled || !cancelled(),"SSTV stream cancelled");};
    cancel();
    const auto helper=QDir(QCoreApplication::applicationDirPath()).filePath("sdrtown_sstv.exe");
    require(QFileInfo(helper).isFile(),"SSTV streaming helper unavailable");
    QTemporaryDir temporary;
    require(temporary.isValid(),"Cannot create SSTV stream temporary directory");
    const auto rgbPath=temporary.filePath("images");
    const bool continuous=!archiveDirectory.isEmpty();
    if(continuous)require(QDir().mkdir(archiveDirectory),"Cannot create SSTV archive session directory");
    QProcess process;
    struct Guard {QProcess& p; ~Guard(){if(p.state()!=QProcess::NotRunning){p.kill();p.waitForFinished(5000);}}} guard{process};
    process.setProgram(helper);
    QStringList arguments{"--stdin","48000",rgbPath,mode,"--progress"};
    if(continuous)arguments<<"--continuous";
    process.setArguments(arguments);
    process.start();
    QElapsedTimer wall,launch,stall,end;
    wall.start(); launch.start(); stall.start();
    while(process.state()==QProcess::Starting) {
        cancel(); require(launch.elapsed()<5000,"SSTV helper launch timed out");
        process.waitForStarted(20);
    }
    require(process.state()==QProcess::Running,"SSTV helper failed to start");
    QByteArray errors;
    SstvRateConverter converter;
    SstvStreamResult result; result.metadata=nlohmann::json::array();
    result.archived=continuous;
    SstvProgress::Complete archive;
    if(continuous)archive=[&](const QImage& image,const QByteArray& line) {
        auto item=nlohmann::json::parse(line.constData(),line.constData()+line.size());
        require(item.at("backend")=="16bf34aac81b0041f5fdce52a1aef64eea0d5f6e","SSTV helper revision mismatch");
        QFile rgb(QDir(rgbPath).filePath(QString::fromStdString(item.at("file").get<std::string>())));
        require(rgb.open(QIODevice::ReadOnly) && rgb.size()==image.width()*image.height()*3,"SSTV archive RGB size mismatch");
        const auto bytes=rgb.readAll();
        require(bytes.size()==rgb.size(),"SSTV archive RGB read failed");
        for(int y=0;y<image.height();++y)
            require(std::memcmp(image.constScanLine(y),bytes.constData()+y*image.width()*3,size_t(image.width()*3))==0,"SSTV archive pixel mismatch");
        rgb.close();
        const auto name=QString("image-%1%2.png").arg(result.savedImages).arg(item.at("complete").get<bool>()?"":".partial");
        const auto path=QDir(archiveDirectory).filePath(name);
        require(!QFileInfo::exists(path),"SSTV archive image already exists");
        QSaveFile saved(path);
        require(saved.open(QIODevice::WriteOnly) && image.save(&saved,"PNG") && saved.commit(),"Cannot save SSTV archive image");
        item["file"]=name.toStdString();item["savedUtc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
        QSaveFile metadata(path+".json");const auto json=item.dump(2);
        require(metadata.open(QIODevice::WriteOnly) && metadata.write(json.data(),qint64(json.size()))==qint64(json.size()) && metadata.commit(),"Cannot save SSTV image metadata");
        require(rgb.remove(),"Cannot remove archived SSTV temporary RGB");
        ++result.savedImages;
        if(result.metadata.size()==64)result.metadata.erase(result.metadata.begin());
        result.metadata.push_back(std::move(item));
        spdlog::info("SSTV autosaved image {} source={} epoch={} rows={} complete={}",result.savedImages,result.sourceId,result.epoch,result.metadata.back().at("rows").get<int>(),result.metadata.back().at("complete").get<bool>());
    };
    SstvProgress progress(preview?preview:SstvPreview([](const auto&,const auto&,int){}),archive);
    bool identity=false,eof=false,haveGap=false;
    uint64_t expected=0,gapGeneration=0;
    qint64 previousPending=0;
    const auto drain=[&] {
        progress.append(process.readAllStandardOutput());
        errors+=process.readAllStandardError();
        require(errors.size()<=65536,"SSTV helper stderr limit exceeded");
    };
    for(;;) {
        cancel(); require(continuous || wall.elapsed()<540000,"SSTV stream wall limit exceeded");
        drain();
        const auto pending=process.bytesToWrite();
        if(pending==0 || pending<previousPending) stall.restart();
        require(pending==0 || stall.elapsed()<5000,"SSTV helper input stalled");
        previousPending=pending;
        if(process.state()==QProcess::NotRunning) break;
        if(eof) {
            require(end.elapsed()<10000,"SSTV helper EOF timed out");
        } else if(pending<=maxPipeBytes-maxBlockBytes) {
            const auto item=next();
            if(std::holds_alternative<SstvStreamEnd>(item)) {
                eof=true; end.start(); process.closeWriteChannel();
            } else if(const auto* block=std::get_if<SstvInputEvent>(&item)) {
                if(block->gapReasons) {
                    require(block->count==0 && !identity,"SSTV input discontinuity; provisional image discarded");
                    require(!haveGap || block->generation>=gapGeneration,"SSTV gap generation moved backwards");
                    haveGap=true; gapGeneration=block->generation;
                } else {
                    require(block->count>0 && block->count<=SstvInputEvent::maxSamples,"Invalid SSTV block size");
                    require(std::isfinite(block->sampleRate) && block->sampleRate>=8000 && block->sampleRate<=96000 &&
                            std::isfinite(block->targetHz) && block->targetHz>0,"Invalid SSTV input identity");
                    require(block->firstSample<=std::numeric_limits<uint64_t>::max()-block->count,"SSTV position overflow");
                    if(!identity) {
                        require(!haveGap || block->generation==gapGeneration,"SSTV generation does not match start gap");
                        converter.start(block->sampleRate);
                        result.sourceId=block->sourceId; result.epoch=block->epoch; result.generation=block->generation;
                        result.inputRate=block->sampleRate; result.targetHz=block->targetHz;
                        expected=block->firstSample; identity=true;
                    }
                    require(block->sourceId==result.sourceId && block->epoch==result.epoch && block->generation==result.generation &&
                            block->sampleRate==result.inputRate && block->targetHz==result.targetHz && block->firstSample==expected,
                            "SSTV stream identity/position changed without a gap");
                    require(result.inputSamples<=std::numeric_limits<uint64_t>::max()-block->count,"SSTV sample counter overflow");
                    require(continuous || double(result.inputSamples+block->count)<=result.inputRate*kSstvMaxDurationSec,"SSTV input sample budget exceeded");
                    const auto converted=converter.process(std::span(block->samples.data(),block->count));
                    require(result.outputSamples<=std::numeric_limits<uint64_t>::max()-converted.size(),"SSTV output counter overflow");
                    require(continuous || result.outputSamples+converted.size()<=uint64_t{48000}*kSstvMaxDurationSec,"SSTV output sample budget exceeded");
                    QByteArray pcm(qsizetype(converted.size()*2),Qt::Uninitialized);
                    for(size_t i=0;i<converted.size();++i) {
                        const auto value=qToLittleEndian(qint16(std::lround(std::clamp(double(converted[i]),-1.,32767./32768.)*32768)));
                        std::memcpy(pcm.data()+i*2,&value,2);
                    }
                    require(process.bytesToWrite()+pcm.size()<=maxPipeBytes,"SSTV pipe budget exceeded");
                    require(process.write(pcm)==pcm.size(),"SSTV helper input write failed");
                    result.inputSamples+=block->count; result.outputSamples+=converted.size(); expected+=block->count;
                }
            }
        }
        // These blocking calls run only on the decoder worker and also pump QProcess IO.
        if(process.bytesToWrite()) process.waitForBytesWritten(5);
        else process.waitForReadyRead(5);
    }
    drain(); cancel();
    if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0)
        throw std::runtime_error("SSTV helper failed: "+errors.left(2048).toStdString());
    require(eof,"SSTV helper exited before source EOF");
    progress.finish();
    for(const auto& line:progress.metadata().split('\n')) {
        if(line.isEmpty()) continue;
        auto item=nlohmann::json::parse(line.constData(),line.constData()+line.size());
        require(item.at("backend")=="16bf34aac81b0041f5fdce52a1aef64eea0d5f6e","SSTV helper revision mismatch");
        const size_t index=result.metadata.size();
        require(index<progress.images().size(),"Missing SSTV stream image");
        const auto& image=progress.images()[index];
        QFile rgb(QDir(rgbPath).filePath(QString("image-%1.rgb").arg(index)));
        require(rgb.open(QIODevice::ReadOnly) && rgb.size()==image.width()*image.height()*3,"SSTV stream RGB file invalid");
        const auto bytes=rgb.readAll();
        require(bytes.size()==rgb.size(),"SSTV stream RGB read failed");
        for(int row=0;row<image.height();++row)
            require(std::memcmp(image.constScanLine(row),bytes.constData()+row*image.width()*3,size_t(image.width()*3))==0,
                    "SSTV stream preview/file mismatch");
        item.erase("file"); // Private temporary RGB is deleted; caller receives owned pixels.
        result.metadata.push_back(std::move(item));
    }
    result.images=progress.images();
    return result;
}
