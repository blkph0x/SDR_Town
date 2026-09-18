#include "RdsMpxFile.h"
#include "ReceiveDecoder.h"
#include "miniaudio.h"
#include <stdexcept>
#include <vector>

RdsMpxSnapshot decodeRdsMpxFile(const std::string& path,size_t chunkSize,
    const std::function<void(const RdsMpxSnapshot&)>& update) {
    if(!chunkSize || chunkSize>8192) throw std::invalid_argument("MPX chunk must be 1..8192 samples");
    ma_decoder decoder{};
    auto config=ma_decoder_config_init(ma_format_f32,0,0);
    if(ma_decoder_init_file(path.c_str(),&config,&decoder)!=MA_SUCCESS)
        throw std::runtime_error("Cannot open MPX recording");
    struct Guard { ma_decoder* d; ~Guard(){ ma_decoder_uninit(d); } } guard{&decoder};
    if(decoder.outputChannels!=1 || decoder.outputSampleRate<128000 || decoder.outputSampleRate>384000)
        throw std::runtime_error("MPX recording must be mono at 128..384 kHz, not speaker audio");
    auto rds=createReceiveDecoder("rds");
    std::vector<float> samples(chunkSize);
    uint64_t position=0;
    const uint64_t limit=uint64_t{decoder.outputSampleRate}*120; // Bounded diagnostics: max two minutes.
    for(;;) {
        ma_uint64 read=0;
        const auto result=ma_decoder_read_pcm_frames(&decoder,samples.data(),chunkSize,&read);
        if(result!=MA_SUCCESS && result!=MA_AT_END) throw std::runtime_error("MPX recording read failed");
        if(!read) break;
        if(position+read>limit) throw std::runtime_error("MPX diagnostic recording exceeds 120 seconds");
        const auto processed=rds->process({std::span(samples.data(),static_cast<size_t>(read)),
            DecoderInputDomain::FmMultiplex,double(decoder.outputSampleRate),0,0,1,position,position==0});
        if(!processed) throw std::runtime_error(processed.detail);
        position+=read;
        if(update) update(std::get<RdsMpxSnapshot>(rds->snapshot()));
    }
    return std::get<RdsMpxSnapshot>(rds->snapshot());
}
