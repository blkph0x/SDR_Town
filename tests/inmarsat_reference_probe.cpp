#include "InmarsatAero.h"
#include <QCoreApplication>
#include <QFile>
#include <iostream>
#include <cstring>

// Developer-only reference harness: raw mono s16le 48 kHz IF or a bounded
// explicitly submitted recording bundle. DEC-0161: cold replay is not live state.
// Writes 8 kHz raw PCM for listening; no RF hardware or user settings touched.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()!=4) {std::cerr<<"input-s16le-48k bit-rate output-s16le-8k\n";return 2;}
    try {
        QFile in(args[1]),out(args[3]);
        if(!in.open(QIODevice::ReadOnly)||!out.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return 2;
        QByteArray bundleIf;
        if(args[1].endsWith(".json",Qt::CaseInsensitive)) {
            if(in.size()>1024*1024) throw std::runtime_error("Recording exceeds 1 MiB");
            const auto bundle=nlohmann::json::parse(in.readAll().toStdString());
            if(bundle.value("schema","")!="sdr-town-inmarsat-recording-v1" ||
               bundle.value("ifRate",0)!=48000 || bundle.value("format","")!="s16le")
                throw std::runtime_error("Unsupported recording format");
            const auto encoded=QByteArray::fromStdString(bundle.at("ifBase64").get<std::string>());
            const auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
            if(!decoded || decoded.decoded.size()>480000 || decoded.decoded.size()%2)
                throw std::runtime_error("Invalid recording samples");
            bundleIf=decoded.decoded;
        }
        const int mode=args[2].toInt();
        InmarsatAero decoder(std::abs(mode),mode<0);
        decoder.setPcmSink([&](std::span<const int16_t> pcm,uint32_t) {
            if(out.write(reinterpret_cast<const char*>(pcm.data()),pcm.size_bytes())!=pcm.size_bytes())
                throw std::runtime_error("PCM write failed");
        });
        uint64_t messages=0;
        decoder.setMessageSink([&](const auto& m){
            ++messages;
            std::cout<<nlohmann::json{{"event","message"},{"aes",m.aesId},{"label",m.label},
                {"text",m.text},{"validated",m.validated},{"position",m.hasPosition}}.dump()<<'\n';
            if(!m.applicationProtocol.empty()) std::cout<<nlohmann::json{
                {"event","application"},{"protocol",m.applicationProtocol},
                {"status",m.applicationStatus},{"text",m.applicationText}}.dump()<<'\n';
        });
        qsizetype offset=0;
        while(offset<bundleIf.size() || !in.atEnd()) {
            const auto bytes=bundleIf.isEmpty()?in.read(960*2):bundleIf.mid(offset,960*2);
            offset+=bytes.size();
            if(bytes.size()%2) return 2;
            std::vector<int16_t> samples(bytes.size()/2);
            std::memcpy(samples.data(),bytes.data(),bytes.size());
            decoder.processIf(samples);
        }
        const auto s=decoder.stats();
        nlohmann::json j={{"input48k",s.input48k},{"softBits",s.softBits},{"crcOk",s.crcOk},
            {"crcBad",s.crcBad},{"cFrames",s.cFrames},{"rejectedCFrames",s.rejectedCFrames},
            {"voiceWords",s.voiceWords},{"pcmSamples",s.pcmSamples},{"codecErrors",s.codecErrors},
            {"codecRepeats",s.codecRepeats},{"codecMutes",s.codecMutes},{"messages",messages},{"positions",s.positions}};
        std::cout<<j.dump(2)<<std::endl;
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<std::endl;return 2;}
}
