#include "SstvProgress.h"
#include "SstvModes.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
void require(bool value,const char* error) {if(!value) throw std::runtime_error(error);}
}
void SstvProgress::append(const QByteArray& bytes) {
    bytes_+=size_t(bytes.size());
    require(bytes_<=4*1024*1024,"SSTV preview transport limit exceeded");
    pending_+=bytes;
    for(;;) {
        const auto end=pending_.indexOf('\n');
        if(end<0) break;
        require(end<=16384,"SSTV preview line limit exceeded");
        const auto record=pending_.left(end).trimmed(); pending_.remove(0,end+1);
        if(!record.isEmpty()) line(record);
    }
    require(pending_.size()<=16384,"SSTV preview line limit exceeded");
}
void SstvProgress::line(const QByteArray& bytes) {
    const auto record=nlohmann::json::parse(bytes.constData(),bytes.constData()+bytes.size());
    require(record.at("schema")==1 && images_.size()<4,"Invalid SSTV preview schema/image count");
    if(record.value("kind",std::string())!="row") {
        require(!record.contains("kind"),"Unknown SSTV preview event");
        require(record.at("file")=="image-"+std::to_string(images_.size())+".rgb","Invalid SSTV preview image sequence");
        if(image_.isNull() && record.at("rows")==0) {
            mode_=QString::fromStdString(record.at("mode").get<std::string>());
            const auto* spec=sstvModeById(record.at("mode").get<std::string>());
            require(spec && record.at("width")==spec->width && record.at("height")==spec->height,"Invalid empty SSTV preview");
            image_=QImage(spec->width,spec->height,QImage::Format_RGB888);
            require(!image_.isNull(),"SSTV preview allocation failed"); image_.fill(Qt::black);
        }
        require(!image_.isNull() && record.at("rows")==rows_ && record.at("height")==image_.height()
            && record.at("width")==image_.width() && record.at("mode")==mode_.toStdString(),"SSTV preview completion mismatch");
        require(!record.at("complete").get<bool>() || rows_==image_.height(),"Missing SSTV preview rows");
        preview_(image_,mode_,rows_);
        images_.push_back(image_); image_=QImage(); seen_.clear(); rows_=0;
        metadata_+=bytes+'\n';
        return;
    }
    require(record.at("image")==images_.size(),"Invalid SSTV preview image index");
    for(const auto* key:{"width","height","row","image"}) require(record.at(key).is_number_integer(),"Noninteger SSTV preview field");
    const auto mode=QString::fromStdString(record.at("mode").get<std::string>());
    const int width=record.at("width").get<int>(),height=record.at("height").get<int>(),row=record.at("row").get<int>();
    require(sstvModeDimensionsOk(mode.toStdString(),width,height),"Invalid SSTV preview dimensions");
    if(image_.isNull()) {
        image_=QImage(width,height,QImage::Format_RGB888);
        require(!image_.isNull(),"SSTV preview allocation failed");
        image_.fill(Qt::black); seen_.assign(height,false); mode_=mode;
    }
    require(record.at("row")==row && image_.height()==height && mode_==mode && row>=0 && row<height && !seen_[row],"Invalid or duplicate SSTV preview row");
    const auto hex=record.at("rgb").get<std::string>();
    require(hex.size()==size_t(width)*6 && std::all_of(hex.begin(),hex.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),"Invalid SSTV preview RGB");
    const auto rgb=QByteArray::fromHex(QByteArray::fromStdString(hex));
    std::memcpy(image_.scanLine(row),rgb.constData(),size_t(width)*3);
    seen_[row]=true; ++rows_;
    // UI observes one latest snapshot; this cadence never changes decoder input.
    if(rows_%8==0) preview_(image_,mode_,rows_);
}
void SstvProgress::finish() {
    require(pending_.isEmpty() && image_.isNull(),"Truncated SSTV preview stream");
}
