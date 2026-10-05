#pragma once
#include "InmarsatAero.h"
#include <algorithm>
#include <array>
#include <cstring>

inline uint32_t inmarsatVoiceAes(const InmarsatAeroStats& s) {
    return s.aes && s.aes<=0xffffff?s.aes:0;
}
inline bool inmarsatSpeechActive(const InmarsatAeroStats& s) {
    return s.lastVoiceSample>0 && s.input48k>=s.lastVoiceSample && s.input48k-s.lastVoiceSample<=24000;
}
inline bool inmarsatVoiceActive(const InmarsatAeroStats& s) {
    return inmarsatVoiceAes(s) && inmarsatSpeechActive(s);
}
template<class Reset>
void inmarsatValidatedVoiceIdentity(InmarsatAeroStats& s,std::span<const uint8_t> su,Reset reset) {
    if(su.size()!=12 || (su[0]!=0x30 && su[0]!=0x60))return;
    const uint32_t aes=(uint32_t(su[1])<<16)|(uint32_t(su[2])<<8)|su[3];
    if(aes && s.aes!=aes) {reset();s.aes=aes;s.lastVoiceSample=0;++s.identityChanges;}
}
// DEC-0156: same 25-word C-frame transaction as the native handler, injectable
// codec call lets tests exercise an API failure without corrupting RF fixtures.
template<class Decode,class Reset,class Sink>
void inmarsatValidatedCFrame(std::span<const uint8_t> words,int validUnits,
    InmarsatAeroStats& s,Decode decode,Reset reset,Sink sink) {
    ++s.cFrames;
    // A C-frame with no CRC-valid subunits is an erasure.  Keep the codec
    // history alive so a short RF fade can be concealed by the next frame.
    // A valid SU carrying a new AES identity is the only normal reset path;
    // that is handled by inmarsatValidatedVoiceIdentity before this callback.
    static_cast<void>(reset);
    if(validUnits==0 || words.size()!=300) {++s.rejectedCFrames;++s.invalidCFrames;return;}
    std::array<int16_t,4000> pcm{};bool speech=false;
    for(int i=0;i<25;++i) {
        char flags[64]{};std::array<int16_t,160> wordPcm{};++s.codecAttemptedWords;
        const int errors=decode(words.data()+i*12,wordPcm.data(),flags);
        if(errors<0) {++s.codecFailures;++s.rejectedCFrames;return;}
        s.codecErrors+=errors;
        if(std::strchr(flags,'R'))++s.codecRepeats;
        const bool muted=std::strchr(flags,'M') || std::strchr(flags,'E') || std::strchr(flags,'T');
        if(muted) {
            ++s.codecMutes;
            std::fill(wordPcm.begin(),wordPcm.end(),int16_t(0));
        } else speech=true;
        std::copy(wordPcm.begin(),wordPcm.end(),pcm.begin()+i*160);
    }
    s.voiceWords+=25;s.pcmSamples+=pcm.size();
    if(speech) {s.lastVoiceSample=s.input48k;++s.speechFrames;if(!inmarsatVoiceAes(s))++s.unidentifiedSpeechFrames;}
    sink(std::span<const int16_t>(pcm),s.aes);
}
