#include "InmarsatDiagnosticRecording.h"
#include "InmarsatAero.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <mutex>

namespace InmarsatDiagnosticRecording {
namespace {
std::mutex mutex;
std::atomic<bool> armed{false};
const void* owner=nullptr;
double frequency=0;
int decoder=0;
QByteArray input, audio;
QByteArray rawIq;
double iqRate=0,iqCenter=0;
uint64_t iqStart=0;
QString state="Idle", started;
QJsonObject firstCounters,lastCounters;
std::chrono::steady_clock::time_point deadline;
constexpr qsizetype inputLimit=48000*5*2, audioLimit=8000*5*2;
void append(QByteArray& bytes,std::span<const int16_t> samples,qsizetype limit) {
    const auto n=std::min<size_t>(samples.size(),size_t((limit-bytes.size())/2));
    const auto offset=bytes.size();bytes.resize(offset+qsizetype(n)*2);
    for(size_t i=0;i<n;++i)qToLittleEndian<int16_t>(samples[i],bytes.data()+offset+i*2);
}
void expire() {
    if(armed && std::chrono::steady_clock::now()>deadline) {
        armed=false;input.clear();audio.clear();rawIq.clear();state="Timed out - no complete recording";
    }
}
}
bool arm(double channelHz) {
    std::lock_guard lock(mutex);
    if(armed || !std::isfinite(channelHz) || channelHz<=0)return false;
    frequency=channelHz;owner=nullptr;decoder=0;input.clear();audio.clear();rawIq.clear();firstCounters={};lastCounters={};
    iqRate=0;iqCenter=0;iqStart=0;
    input.reserve(inputLimit);audio.reserve(audioLimit);
    started=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    state="Waiting for selected channel";deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
    armed=true;return true;
}
void cancel() {std::lock_guard lock(mutex);armed=false;owner=nullptr;input.clear();audio.clear();rawIq.clear();state="Idle";}
QString status() {std::lock_guard lock(mutex);expire();return state;}
void begin(const void* source,double channelHz,int mode,bool reset,std::span<const int16_t> samples) {
    if(!armed.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);expire();if(!armed)return;
    if(owner && owner!=source)return;
    if(owner && (reset || channelHz!=frequency || mode!=decoder)) {
        armed=false;input.clear();audio.clear();rawIq.clear();state="Cancelled - channel changed or input gap";return;
    }
    if(channelHz!=frequency)return;
    owner=source;decoder=mode;state="Recording (maximum five seconds)";
    append(input,samples,inputLimit);
}
void pcm(const void* source,std::span<const int16_t> samples) {
    if(!armed.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);
    if(armed && owner==source)append(audio,samples,audioLimit);
}
void end(const void* source) {
    if(!armed.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);
    if(armed && source==owner && input.size()==inputLimit) {armed=false;state="Ready for review";}
}
void release(const void* source) {
    if(!armed.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);
    if(armed && source==owner) {armed=false;owner=nullptr;input.clear();audio.clear();rawIq.clear();state="Cancelled - decoder stopped";}
}
void counters(const void* source,const InmarsatAeroStats& s) {
    if(!armed.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);if(!armed || source!=owner)return;
    lastCounters={{"input48k",double(s.input48k)},{"crcOk",double(s.crcOk)},
        {"crcBad",double(s.crcBad)},{"pcmSamples",double(s.pcmSamples)},
        {"codecErrors",double(s.codecErrors)},{"codecMutes",double(s.codecMutes)},
        {"acarsAirToGround",double(s.acarsAirToGround)},{"acarsGroundToAir",double(s.acarsGroundToAir)},
        {"acarsUnknownDirection",double(s.acarsUnknownDirection)},{"adscDecoded",double(s.adscDecoded)},
        {"positionReports",double(s.positions)},{"positionIdentityMismatches",double(s.positionIdentityMismatches)},
        {"applicationDecoded",double(s.applicationDecoded)},{"applicationInvalid",double(s.applicationInvalid)},
        {"applicationUnsupported",double(s.applicationUnsupported)},{"applicationControl",double(s.applicationControl)}};
    if(firstCounters.isEmpty())firstCounters=lastCounters;
}
void iq(const void* source,std::span<const std::complex<float>> samples,double rate,double center,uint64_t start) {
    if(!armed.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);if(!armed || source!=owner)return;
    if(rawIq.isEmpty()) {iqRate=rate;iqCenter=center;iqStart=start;}
    const size_t count=std::min<size_t>(samples.size(),16384-size_t(rawIq.size()/8));
    const auto offset=rawIq.size();rawIq.resize(offset+count*8);
    for(size_t i=0;i<count;++i) {
        qToLittleEndian<uint32_t>(std::bit_cast<uint32_t>(samples[i].real()),rawIq.data()+offset+i*8);
        qToLittleEndian<uint32_t>(std::bit_cast<uint32_t>(samples[i].imag()),rawIq.data()+offset+i*8+4);
    }
}
QByteArray bundle() {
    std::lock_guard lock(mutex);
    if(state!="Ready for review")return {};
    return QJsonDocument(QJsonObject{{"schema","sdr-town-inmarsat-recording-v1"},
        {"channelHz",frequency},{"mode",decoder},{"timeUtc",started},
        {"ifRate",48000},{"pcmRate",8000},{"format","s16le"},
        {"before",firstCounters},{"after",lastCounters},
        {"iqBase64",QString::fromLatin1(rawIq.toBase64())},{"iqRate",iqRate},
        {"iqCenterHz",iqCenter},{"iqStartSample",QString::number(iqStart)},{"iqFormat","cf32_le"},
        {"ifBase64",QString::fromLatin1(input.toBase64())},
        {"pcmBase64",QString::fromLatin1(audio.toBase64())}}).toJson(QJsonDocument::Compact);
}
}
