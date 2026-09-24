#include "InmarsatAdsc.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace {
int hex(char c) {
    if(c>='0' && c<='9') return c-'0';
    if(c>='A' && c<='F') return c-'A'+10;
    if(c>='a' && c<='f') return c-'a'+10;
    return -1;
}
uint16_t crc(std::string_view prefix, const std::vector<uint8_t>& payload) {
    uint16_t state=0xffff;
    const auto byte=[&](uint8_t value) {
        state^=uint16_t(value)<<8;
        for(int i=0;i<8;++i) state=(state&0x8000)?uint16_t((state<<1)^0x1021):uint16_t(state<<1);
    };
    for(auto c:prefix) byte(uint8_t(c));
    for(auto c:payload) byte(c);
    return uint16_t(~state);
}
uint32_t bits(const uint8_t* data, unsigned offset, unsigned count) {
    uint32_t value=0;
    for(unsigned i=offset;i<offset+count;++i) value=(value<<1)|((data[i/8]>>(7-i%8))&1);
    return value;
}
int32_t signedBits(uint32_t value,unsigned width) {
    return (value&(1u<<(width-1)))?int32_t(value)-int32_t(1u<<width):int32_t(value);
}
}
std::optional<InmarsatAdscPosition> InmarsatAdsc::parse(std::string_view text) {
    // JAERO arincparse.cpp (ARINC622/745) and libacars adsc.c, independent cross-check.
    if(text.size()>8192) return {};
    const auto slash=text.find('/');
    if(slash==std::string_view::npos) return {};
    const auto dot=text.find(".ADS",slash);
    if(dot==std::string_view::npos) return {};
    const auto body=text.substr(dot+1);
    if(body.size()<16 || (body.size()-14)%2) return {};
    const auto prefix=body.substr(0,10); // IMI + fixed-width seven-character registration
    const auto encoded=body.substr(10,body.size()-14);
    std::vector<uint8_t> payload;
    for(size_t i=0;i<encoded.size();i+=2) {
        const int hi=hex(encoded[i]),lo=hex(encoded[i+1]);
        if(hi<0 || lo<0) return {};
        payload.push_back(uint8_t(hi*16+lo));
    }
    uint16_t received=0;
    for(char c:body.substr(body.size()-4)) { const int h=hex(c); if(h<0)return {}; received=uint16_t(received*16+h); }
    if(received!=crc(prefix,payload)) return {};
    InmarsatAdscPosition p;
    p.registration=std::string(prefix.substr(3));
    p.registration.erase(0,p.registration.find_first_not_of('.'));
    bool position=false;
    for(size_t offset=0;offset<payload.size();) {
        const auto type=payload[offset]; size_t size=0;
        switch(type) {
        case 3:size=2;break;
        case 6:size=1;break;
        case 7:case 9:case 10:case 18:case 19:case 20:size=11;break;
        case 12:size=7;break;
        case 13:size=18;break;
        case 14:case 15:size=6;break;
        case 16:size=5;break;
        case 17:size=4;break;
        case 22:size=9;break;
        case 23:size=10;break;
        default:return {}; // Unknown/variable tags must not misalign the next group.
        }
        if(size>payload.size()-offset) return {};
        const auto* d=payload.data()+offset+1;
        if(size==11) {
            if(position) return {}; // Do not silently choose between multiple basic reports.
            p.latitude=signedBits(bits(d,0,21),21)*(90.0/524288);
            p.longitude=signedBits(bits(d,21,21),21)*(90.0/524288);
            p.altitudeFt=signedBits(bits(d,42,16),16)*4.0;
            p.secondsPastHour=bits(d,58,15)*0.125;
            if(std::abs(p.latitude)>90 || std::abs(p.longitude)>180 || p.secondsPastHour>=3600) return {};
            position=true;
        } else if(type==12) {
            for(unsigned i=0;i<8;++i) {
                auto c=bits(d,i*6,6);
                p.callsign+=char(c<=26?c|0x40:c);
            }
            while(!p.callsign.empty() && p.callsign.back()==' ') p.callsign.pop_back();
        } else if(type==17) p.airframeId=bits(d,0,24);
        offset+=size;
    }
    return position?std::optional(p):std::nullopt;
}
