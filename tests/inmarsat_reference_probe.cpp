#include "InmarsatAero.h"
#include <QCoreApplication>
#include <QFile>
#include <iostream>
#include <cstring>

// Developer-only independent reference harness: raw mono s16le 48 kHz IF.
// Writes 8 kHz raw PCM for listening; no RF hardware or user settings touched.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()!=4) {std::cerr<<"input-s16le-48k bit-rate output-s16le-8k\n";return 2;}
    try {
        QFile in(args[1]),out(args[3]);
        if(!in.open(QIODevice::ReadOnly)||!out.open(QIODevice::WriteOnly)) return 2;
        const int mode=args[2].toInt();
        InmarsatAero decoder(std::abs(mode),mode<0);
        decoder.setPcmSink([&](std::span<const int16_t> pcm,uint32_t) {
            if(out.write(reinterpret_cast<const char*>(pcm.data()),pcm.size_bytes())!=pcm.size_bytes())
                throw std::runtime_error("PCM write failed");
        });
        uint64_t messages=0;
        decoder.setMessageSink([&](const auto&){++messages;});
        while(!in.atEnd()) {
            const auto bytes=in.read(960*2);
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
