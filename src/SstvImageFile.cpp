#include "SstvImageFile.h"
#include "miniaudio.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {
constexpr auto revision="16bf34aac81b0041f5fdce52a1aef64eea0d5f6e";
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
}

nlohmann::json decodeSstvImageFile(const QString& input,const QString& output,const QString& mode,
                                  const std::function<bool()>& cancelled) {
    const auto checkCancelled=[&] {
        if(cancelled && cancelled()) throw std::runtime_error("SSTV decode cancelled");
    };
    checkCancelled();
    require(mode=="auto" || mode=="robot36" || mode=="martin1","Supported image modes: auto, robot36, martin1");
    const QFileInfo info(input);
    require(info.isFile() && info.size()<=128*1024*1024,"SSTV image input must be a file <=128 MiB");
    require(!QFileInfo::exists(output),"SSTV output directory already exists; choose a new directory");
    const auto helper=QDir(QCoreApplication::applicationDirPath()).filePath("sdrtown_sstv.exe");
    require(QFileInfo(helper).isFile(),"SSTV image backend is unavailable beside the application");
    QTemporaryDir temporary;
    require(temporary.isValid(),"Cannot create SSTV temporary directory");
    const auto pcmPath=temporary.filePath("input.pcm");
    const auto rgbPath=temporary.filePath("images");
    ma_decoder decoder{};
    const auto config=ma_decoder_config_init(ma_format_f32,0,0);
#ifdef _WIN32
    const auto opened=ma_decoder_init_file_w(input.toStdWString().c_str(),&config,&decoder);
#else
    const auto opened=ma_decoder_init_file(input.toUtf8().constData(),&config,&decoder);
#endif
    require(opened==MA_SUCCESS,"Cannot open SSTV image recording");
    struct Guard { ma_decoder* p; ~Guard(){ma_decoder_uninit(p);} } guard{&decoder};
    const auto rate=decoder.outputSampleRate;
    require(decoder.outputChannels==1 && rate>=8000 && rate<=96000,"SSTV image audio must be mono at 8..96 kHz");
    const uint64_t limit=uint64_t{rate}*360; // DEC-0092 recorded job resource budget.
    ma_uint64 length=0;
    require(ma_decoder_get_length_in_pcm_frames(&decoder,&length)!=MA_SUCCESS || length<=limit,"SSTV image audio exceeds 360 seconds");
    QFile pcm(pcmPath);
    require(pcm.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot create temporary PCM");
    std::array<float,4096> samples{};
    std::array<qint16,4096> integers{};
    uint64_t total=0;
    QCryptographicHash pcmHash(QCryptographicHash::Sha256);
    for(;;) {
        checkCancelled();
        ma_uint64 count=0;
        const auto status=ma_decoder_read_pcm_frames(&decoder,samples.data(),samples.size(),&count);
        require(status==MA_SUCCESS || status==MA_AT_END,"SSTV image audio read failed");
        if(!count) break;
        require(count<=limit-total,"SSTV image audio exceeds 360 seconds");
        for(size_t i=0;i<count;++i) {
            require(std::isfinite(samples[i]),"Non-finite SSTV audio");
            const auto value=static_cast<qint16>(std::lround(std::clamp(double(samples[i]),-1.0,32767.0/32768.0)*32768.0));
            integers[i]=qToLittleEndian(value);
        }
        const QByteArrayView bytes(reinterpret_cast<const char*>(integers.data()),qsizetype(count*2));
        require(pcm.write(bytes.data(),bytes.size())==bytes.size(),"Temporary PCM write failed");
        pcmHash.addData(bytes); total+=count;
    }
    pcm.close();
    checkCancelled();
    QProcess worker;
    worker.setProgram(helper);
    worker.setArguments({pcmPath,QString::number(rate),rgbPath,mode});
    worker.start();
    require(worker.waitForStarted(5000),"SSTV backend did not start");
    QElapsedTimer elapsed; elapsed.start();
    QByteArray stdoutBytes,stderrBytes;
    for(;;) {
        worker.waitForFinished(50);
        stdoutBytes+=worker.readAllStandardOutput();
        stderrBytes+=worker.readAllStandardError();
        if(cancelled && cancelled()) {
            worker.kill(); worker.waitForFinished(5000);
            throw std::runtime_error("SSTV decode cancelled");
        }
        if(stdoutBytes.size()+stderrBytes.size()>65536 || elapsed.elapsed()>120000) {
            worker.kill(); worker.waitForFinished(5000);
            throw std::runtime_error("SSTV backend exceeded time/log limits");
        }
        if(worker.state()==QProcess::NotRunning) break;
    }
    if(worker.exitStatus()!=QProcess::NormalExit || worker.exitCode()!=0)
        throw std::runtime_error("SSTV backend failed: "+stderrBytes.left(2048).toStdString());
    auto images=nlohmann::json::array();
    std::vector<QImage> decoded;
    for(const auto& line:stdoutBytes.split('\n')) {
        if(line.trimmed().isEmpty()) continue;
        require(images.size()<4,"SSTV backend image limit exceeded");
        auto item=nlohmann::json::parse(line.constData(),line.constData()+line.size());
        const auto name="image-"+std::to_string(images.size())+".rgb";
        require(item.at("schema")==1 && item.at("backend")==revision && item.at("file")==name,"Invalid SSTV backend response");
        const auto found=item.at("mode").get<std::string>();
        const int w=item.at("width").get<int>(),h=item.at("height").get<int>();
        const int rows=item.at("rows").get<int>();
        const bool complete=item.at("complete").get<bool>();
        require(w==320 && ((found=="robot36" && h==240)||(found=="martin1" && h==256)),"Invalid SSTV image mode/dimensions");
        require(rows>=0 && rows<=h && (!complete || rows==h),"Invalid SSTV image row count");
        QFile rgb(QDir(rgbPath).filePath(QString::fromStdString(name)));
        require(rgb.open(QIODevice::ReadOnly) && rgb.size()==w*h*3,"Invalid SSTV RGB output");
        const auto bytes=rgb.readAll();
        require(bytes.size()==w*h*3,"SSTV RGB read failed");
        decoded.push_back(QImage(reinterpret_cast<const uchar*>(bytes.constData()),w,h,w*3,QImage::Format_RGB888).copy());
        require(!decoded.back().isNull(),"SSTV image allocation failed");
        const auto png="image-"+std::to_string(images.size())+(complete?".png":".partial.png");
        item["file"]=png;
        item["rgbSha256"]=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();
        images.push_back(std::move(item));
    }
    nlohmann::json result{{"decoder","sstv-images"},{"backend",revision},{"sampleRate",rate},{"samples",total},
        {"requestedMode",mode.toStdString()},{"pcmSha256",pcmHash.result().toHex().toStdString()},
        {"images",images},{"outputDirectory",QFileInfo(output).absoluteFilePath().toStdString()}};
    // No caller-visible output before all backend results pass validation.
    // DEC-0093: cancellation is accepted until publication; finish saving once begun.
    checkCancelled();
    require(QDir(QFileInfo(output).absolutePath()).mkdir(QFileInfo(output).fileName()),"Cannot create new SSTV output directory");
    for(size_t i=0;i<decoded.size();++i) {
        QSaveFile file(QDir(output).filePath(QString::fromStdString(images[i]["file"].get<std::string>())));
        require(file.open(QIODevice::WriteOnly) && decoded[i].save(&file,"PNG") && file.commit(),"SSTV PNG save failed");
    }
    QSaveFile report(QDir(output).filePath("sstv-report.json"));
    const auto json=result.dump(2);
    require(report.open(QIODevice::WriteOnly) && report.write(json.data(),qint64(json.size()))==qint64(json.size()) && report.commit(),"SSTV report save failed");
    return result;
}
