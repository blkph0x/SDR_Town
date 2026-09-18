#include "SstvVis.h"
#include "miniaudio.h"
#include <filesystem>
#include <stdexcept>

SstvVisReport inspectSstvAudioFile(const std::string& path,size_t chunk) {
    if(!chunk || chunk>8192) throw std::invalid_argument("SSTV chunk must be 1..8192 samples");
    const auto nativePath=std::filesystem::u8path(path);
    std::error_code fileError;
    const auto bytes=std::filesystem::file_size(nativePath,fileError);
    if(fileError) throw std::runtime_error("Cannot open SSTV recording");
    if(bytes>64*1024*1024) throw std::runtime_error("SSTV recording exceeds 64 MiB");
    ma_decoder decoder{};
    auto config=ma_decoder_config_init(ma_format_f32,0,0);
    // CLI QString supplies UTF-8. Narrow fopen_s would use the Windows code page.
#ifdef _WIN32
    const auto opened=ma_decoder_init_file_w(nativePath.c_str(),&config,&decoder);
#else
    const auto opened=ma_decoder_init_file(nativePath.c_str(),&config,&decoder);
#endif
    if(opened!=MA_SUCCESS)
        throw std::runtime_error("Cannot open SSTV recording");
    struct Guard { ma_decoder* p; ~Guard(){ma_decoder_uninit(p);} } guard{&decoder};
    if(decoder.outputChannels!=1 || decoder.outputSampleRate<8000 || decoder.outputSampleRate>96000)
        throw std::runtime_error("SSTV recording must be mono at 8..96 kHz");
    SstvVisDetector vis(decoder.outputSampleRate);
    std::vector<float> samples(chunk);
    std::vector<SstvVisEvent> headers;
    const uint64_t limit=uint64_t{decoder.outputSampleRate}*120; // DEC-0091 diagnostic budget.
    ma_uint64 length=0;
    if(ma_decoder_get_length_in_pcm_frames(&decoder,&length)==MA_SUCCESS && length>limit)
        throw std::runtime_error("SSTV recording exceeds 120 seconds");
    uint64_t position=0;
    for(;;) {
        ma_uint64 count=0;
        const auto result=ma_decoder_read_pcm_frames(&decoder,samples.data(),chunk,&count);
        if(result!=MA_SUCCESS && result!=MA_AT_END) throw std::runtime_error("SSTV recording read failed");
        if(!count) break;
        if(count>limit-position) throw std::runtime_error("SSTV recording exceeds 120 seconds");
        auto found=vis.process(std::span(samples.data(),static_cast<size_t>(count)));
        headers.insert(headers.end(),found.begin(),found.end());
        position+=count;
    }
    auto report=vis.counters();
    report.headers=std::move(headers);
    return report;
}
