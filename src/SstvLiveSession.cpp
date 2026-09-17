#include "SstvLiveSession.h"
#include "SstvStreamWorker.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSaveFile>
#include <cmath>
#include <stdexcept>

namespace {void require(bool value,const char* message){if(!value) throw std::runtime_error(message);}}
nlohmann::json decodeSstvLive(const std::shared_ptr<SstvReceiverFeed>& feed,
    const std::function<void()>& validateReceiver,const QString& output,const QString& mode,
    const std::function<bool()>& finish,const std::function<bool()>& cancel,const SstvPreview& preview) {
    require(bool(feed) && bool(validateReceiver),"Missing SSTV receiver");
    require(!QFileInfo::exists(output) && QDir(QFileInfo(output).absolutePath()).exists(),"Choose a new SSTV output folder under an existing directory");
    validateReceiver();
    auto queue=feed->attach();
    struct Guard {std::shared_ptr<SstvReceiverFeed> feed;std::shared_ptr<SstvLiveInput> queue;~Guard(){feed->detach(queue);}} guard{feed,queue};
    QElapsedTimer duration; duration.start();
    double seconds=0;
    bool finishing=false;
    const auto result=decodeSstvStream([&]()->SstvStreamItem {
        validateReceiver();
        if(!finishing && finish && finish()) {feed->finish(queue);finishing=true;}
        if(duration.elapsed()>=360000 || seconds>=360) return SstvStreamEnd{};
        if(auto item=queue->pop()) {
            if(item->count && item->sampleRate>0) {
                const size_t remaining=size_t(std::max(0.,std::floor((360-seconds)*item->sampleRate)));
                item->count=std::min(item->count,remaining);
                if(!item->count) return SstvStreamEnd{};
                seconds+=double(item->count)/item->sampleRate;
            }
            return *item;
        }
        if(finishing) return SstvStreamEnd{};
        return SstvStreamIdle{};
    },mode,cancel,preview);
    feed->detach(queue);
    require(!cancel || !cancel(),"SSTV live session cancelled");
    auto images=result.metadata;
    for(size_t i=0;i<images.size();++i)
        images[i]["file"]="image-"+std::to_string(i)+(images[i].at("complete").get<bool>()?".png":".partial.png");
    nlohmann::json report{{"decoder","sstv-live"},{"images",images},
        {"outputDirectory",QFileInfo(output).absoluteFilePath().toStdString()},
        {"inputRate",result.inputRate},{"inputSamples",result.inputSamples},{"outputSamples",result.outputSamples},
        {"targetHz",result.targetHz},{"sourceId",result.sourceId},{"epoch",result.epoch},{"generation",result.generation}};
    require(QDir(QFileInfo(output).absolutePath()).mkdir(QFileInfo(output).fileName()),"Cannot create new SSTV output folder");
    for(size_t i=0;i<result.images.size();++i) {
        QSaveFile image(QDir(output).filePath(QString::fromStdString(images[i].at("file").get<std::string>())));
        require(image.open(QIODevice::WriteOnly) && result.images[i].save(&image,"PNG") && image.commit(),"Cannot save SSTV live image");
    }
    QSaveFile file(QDir(output).filePath("sstv-report.json")); const auto json=report.dump(2);
    require(file.open(QIODevice::WriteOnly) && file.write(json.data(),qint64(json.size()))==qint64(json.size()) && file.commit(),"Cannot save SSTV live report");
    return report;
}
