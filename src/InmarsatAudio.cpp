#include "InmarsatAudio.h"
#include "miniaudio.h"
#include <QFile>
#include <QtEndian>
#include <array>
#include <atomic>
#include <algorithm>
#include <stdexcept>

struct InmarsatAudio::Impl {
    static constexpr size_t capacity=32768; // Power-of-two, 4.096 s @ 8 kHz.
    std::array<int16_t,capacity> ring{};
    std::atomic<uint64_t> read{0},write{0},underflow{0},dropped{0};
    ma_device device{};
    bool play=false,open=false;
    QFile wav;
    uint32_t wavSamples=0;
    QString deviceError;
    void finishWav() {
        if(!wav.isOpen()) return;
        uchar value[4];
        qToLittleEndian<uint32_t>(36+wavSamples*2,value);
        bool ok=wav.seek(4) && wav.write(reinterpret_cast<char*>(value),4)==4;
        qToLittleEndian<uint32_t>(wavSamples*2,value);
        ok=ok && wav.seek(40) && wav.write(reinterpret_cast<char*>(value),4)==4 && wav.flush();
        wav.close();
        if(!ok) throw std::runtime_error("Could not finalize Aero WAV header");
    }
    static void callback(ma_device* device,void* output,const void*,ma_uint32 frames) {
        auto& s=*static_cast<Impl*>(device->pUserData);
        const auto r=s.read.load(std::memory_order_relaxed),w=s.write.load(std::memory_order_acquire);
        const size_t count=std::min<size_t>(frames,w-r);
        auto* pcm=static_cast<int16_t*>(output);
        for(size_t i=0;i<count;++i) pcm[i]=s.ring[(r+i)&(capacity-1)];
        std::fill(pcm+count,pcm+frames,0);
        s.read.store(r+count,std::memory_order_release);
        s.underflow.fetch_add(frames-count,std::memory_order_relaxed);
    }
    void start() {
        auto config=ma_device_config_init(ma_device_type_playback);
        config.playback.format=ma_format_s16; config.playback.channels=1;
        config.sampleRate=8000; config.dataCallback=callback; config.pUserData=this;
        if(ma_device_init(nullptr,&config,&device)!=MA_SUCCESS) {deviceError="Default playback device unavailable";play=false;return;}
        open=true;
        if(ma_device_start(&device)!=MA_SUCCESS) {deviceError="Default playback device did not start";ma_device_uninit(&device);open=false;play=false;}
    }
};
InmarsatAudio::InmarsatAudio(bool playback,const QString& path):impl_(std::make_unique<Impl>()) {
    impl_->play=playback;
    if(!path.isEmpty()) {
        impl_->wav.setFileName(path);
        if(!impl_->wav.open(QIODevice::WriteOnly|QIODevice::NewOnly)) throw std::runtime_error("Cannot create new Aero WAV; choose a new output path");
        QByteArray h(44,0); std::copy_n("RIFF",4,h.data()); std::copy_n("WAVEfmt ",8,h.data()+8);
        qToLittleEndian<uint32_t>(16,h.data()+16); qToLittleEndian<uint16_t>(1,h.data()+20);
        qToLittleEndian<uint16_t>(1,h.data()+22); qToLittleEndian<uint32_t>(8000,h.data()+24);
        qToLittleEndian<uint32_t>(16000,h.data()+28); qToLittleEndian<uint16_t>(2,h.data()+32);
        qToLittleEndian<uint16_t>(16,h.data()+34); std::copy_n("data",4,h.data()+36);
        if(impl_->wav.write(h)!=h.size()) throw std::runtime_error("Cannot write Aero WAV header");
    }
}
InmarsatAudio::~InmarsatAudio() {
    if(impl_->open) ma_device_uninit(&impl_->device);
    try { impl_->finishWav(); } catch (...) {} // Explicit finish() reports failures to the session.
}
void InmarsatAudio::finish() { impl_->finishWav(); }
void InmarsatAudio::push(std::span<const int16_t> pcm) {
    auto& s=*impl_;
    if(s.wav.isOpen()) {
        // A diagnostics run cannot silently fill the disk. Stop with a clear error.
        if(uint64_t(s.wavSamples)+pcm.size()>128*1024*1024) throw std::runtime_error("Aero WAV reached 256 MiB recording limit");
        QByteArray bytes(pcm.size_bytes(),0);
        for(size_t i=0;i<pcm.size();++i) qToLittleEndian<int16_t>(pcm[i],bytes.data()+2*i);
        if(s.wav.write(bytes)!=bytes.size()) throw std::runtime_error("Aero WAV write failed");
        s.wavSamples+=pcm.size();
    }
    if(!s.play) return;
    const auto w=s.write.load(std::memory_order_relaxed),r=s.read.load(std::memory_order_acquire);
    if(pcm.size()>Impl::capacity-(w-r)) {s.dropped.fetch_add(pcm.size());return;}
    for(size_t i=0;i<pcm.size();++i) s.ring[(w+i)&(Impl::capacity-1)]=pcm[i];
    s.write.store(w+pcm.size(),std::memory_order_release);
    // Queue the complete first C frame before starting the device.
    if(!s.open) s.start();
}
void InmarsatAudio::discardPlayback() {
    auto& s=*impl_;
    if(s.open) {ma_device_uninit(&s.device);s.open=false;}
    s.read.store(s.write.load());
}
size_t InmarsatAudio::queued() const {
    if(!impl_->open) return 0;
    const auto r=impl_->read.load(),w=impl_->write.load(); return size_t(w-r);
}
nlohmann::json InmarsatAudio::report() const {
    const auto& s=*impl_;
    return {{"speakerQueued",queued()},{"speakerDropped",s.dropped.load()},
        {"speakerZeroFill",s.underflow.load()},{"speakerError",s.deviceError.toStdString()},
        {"wavPath",s.wav.fileName().toStdString()},{"wavSamples",s.wavSamples},{"recording",s.wav.isOpen()}};
}
