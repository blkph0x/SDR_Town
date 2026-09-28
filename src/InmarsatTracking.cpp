#include "InmarsatTracking.h"
#include "InmarsatIdentity.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
bool numeric(const nlohmann::json& j, const char* key, double& value) {
    auto it=j.find(key);
    if(it==j.end() || !it->is_number())return false;
    value=it->get<double>();return std::isfinite(value);
}
uint32_t hexId(const nlohmann::json& row) {
    auto it=row.find("hex");
    if(it==row.end() || !it->is_string())return 0;
    const auto s=it->get<std::string>();if(s.size()!=6)return 0;
    uint32_t id=0;
    for(char c:s) {
        const int n=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;
        if(n<0)return 0;id=(id<<4)|n;
    }
    return inmarsatClassicIcao(id).empty()?0:id;
}
bool ageValid(double now,double received,double ttl) {
    // DEC-0164: compare the deadline directly. Subtraction can round the
    // exact expiry just below ttl on fractional monotonic clocks.
    return std::isfinite(received) && received>0 && now>=received && now<received+ttl;
}
uint32_t voiceId(const nlohmann::json& report) {
    if(!report.is_object() || !report.value("voiceActive",false))return 0;
    const auto it=report.find("voiceAesId");
    if(it==report.end() || !it->is_number_integer())return 0;
    const auto id=it->get<int64_t>();return id>0&&id<0xffffff?uint32_t(id):0;
}
}

void InmarsatTracking::setRf(std::vector<InmarsatAircraft> aircraft,double now) {
    std::sort(aircraft.begin(),aircraft.end(),[](const auto& a,const auto& b){return a.lastSeenMonotonic>b.lastSeenMonotonic;});
    rf_.clear();
    for(auto& a:aircraft) {
        if(rf_.size()==512)break; // DEC-0164: union of two independent 256-entry registries.
        if(!a.identity.validated || inmarsatClassicIcao(a.identity.aesId).empty() ||
            !ageValid(now,a.lastSeenMonotonic,identityTtl))continue;
        rf_.try_emplace(a.identity.aesId,std::move(a));
    }
    std::erase_if(online_,[&](const auto& entry){return !rf_.contains(entry.first) || !ageValid(now,entry.second.observed,onlineTtl);});
}
void InmarsatTracking::setOnlineEnabled(bool enabled) {
    enabled_=enabled;if(!enabled)online_.clear();
}
std::vector<uint32_t> InmarsatTracking::eligibleIds(double now) const {
    std::vector<uint32_t> ids;
    for(const auto& [id,a]:rf_)
        if(a.identity.classicAeroIdentity && a.identity.icaoHex==inmarsatClassicIcao(id) &&
            ageValid(now,a.lastSeenMonotonic,identityTtl))ids.push_back(id);
    return ids;
}
bool InmarsatTracking::acceptOnline(const nlohmann::json& body,const std::set<uint32_t>& requested,double now,double utc) {
    if(!enabled_ || !body.is_object())return false;
    double timestamp=0;
    // ADSB.lol /v2/hex 'now' is Unix milliseconds (live API verified DEC-0164).
    // Reject cached/future envelopes, not just
    // stale aircraft rows. A clock mismatch is visible as a rejected response.
    if(!numeric(body,"now",timestamp))return false;
    timestamp/=1000.0;
    if(timestamp>utc+5 || utc-timestamp>onlineTtl)return false;
    const auto rows=body.find("ac");
    if(rows==body.end() || !rows->is_array() || rows->size()>1000)return false;
    const auto eligible=eligibleIds(now);
    std::set<uint32_t> seen;
    for(const auto& row:*rows) {
        const auto id=hexId(row);Position p;double age=0;
        if(!id || !requested.contains(id) || !std::binary_search(eligible.begin(),eligible.end(),id) ||
            !seen.insert(id).second || !numeric(row,"lat",p.lat) || !numeric(row,"lon",p.lon) ||
            std::abs(p.lat)>90 || std::abs(p.lon)>180 || !numeric(row,"seen_pos",age) || age<0) {++rejected_;continue;}
        age+=std::max(0.0,utc-timestamp);
        if(age>=onlineTtl) {++rejected_;continue;}
        p.observed=now-age;
        p.hasAltitude=numeric(row,"alt_baro",p.alt);
        p.motion=numeric(row,"track",p.track) && numeric(row,"gs",p.speed) &&
            p.track>=0 && p.track<360 && p.speed>=0 && p.speed<=1200;
        // The observation time, not HTTP receipt time, owns freshness. Repeated
        // fixes cannot extend TTL or pull an aircraft backwards in time.
        auto old=online_.find(id);
        if(old!=online_.end() && p.observed<=old->second.observed) {++duplicate_;continue;}
        online_[id]=p;++accepted_;
    }
    return true;
}
nlohmann::json InmarsatTracking::counters() const {
    return {{"mapOnlineAccepted",accepted_},{"mapOnlineRejected",rejected_},{"mapOnlineOlder",duplicate_}};
}
nlohmann::json InmarsatTracking::report(const nlohmann::json& input,double now,bool estimates) const {
    const auto activity=input.is_object()?input:nlohmann::json::object();
    auto result=activity;
    if(!activity.value("receptionRunning",true)) {result["voiceActive"]=false;result["speechActive"]=false;}
    result["positions"]=nlohmann::json::array();result["aircraft"]=nlohmann::json::array();
    std::set<uint32_t> voices;
    if(activity.value("receptionRunning",true)) {
        if(auto id=voiceId(activity))voices.insert(id);
        if(activity.contains("watch") && activity["watch"].contains("channels"))
            for(const auto& channel:activity["watch"]["channels"])
                if(channel.contains("decoder"))if(auto id=voiceId(channel["decoder"]))voices.insert(id);
    }
    result["activeAesIds"]=voices;
    uint64_t unknownVoice=0;
    if(activity.value("receptionRunning",true)) {
        const auto unknown=[](const nlohmann::json& r){return r.value("speechActive",false) && voiceId(r)==0;};
        if(activity.contains("watch") && activity["watch"].contains("channels")) {
            for(const auto& ch:activity["watch"]["channels"])if(ch.contains("decoder") && unknown(ch["decoder"]))++unknownVoice;
        } else if(unknown(activity))++unknownVoice;
    }
    const auto audio=activity.value("audio",nlohmann::json::object());
    result["speakerAesId"]=audio.value("speakerRunning",false)?audio.value("audioAesId",uint32_t{0}):0;
    uint64_t rfCount=0,onlineCount=0,estimateCount=0,unlocated=0,stale=0,unmappedVoice=voices.size();
    for(const auto& [id,a]:rf_) {
        if(!ageValid(now,a.lastSeenMonotonic,identityTtl))continue;
        const auto& m=a.identity;
        Position p;bool have=false;std::string source,reason="no_position";
        const bool rfValid=m.hasPosition && ageValid(now,a.positionMonotonic,rfTtl) &&
            std::isfinite(m.latDeg) && std::isfinite(m.lonDeg) && std::abs(m.latDeg)<=90 && std::abs(m.lonDeg)<=180;
        double rfObserved=a.positionMonotonic;
        if(m.positionHasTimestamp && m.positionSecondsPastHour>=0 && m.positionSecondsPastHour<3600) {
            double delay=std::fmod(a.positionTime,3600.0)-m.positionSecondsPastHour;
            if(delay < -5)delay+=3600;
            rfObserved-=std::max(0.0,delay);
        }
        auto net=online_.find(id);
        const bool netValid=enabled_ && net!=online_.end() && ageValid(now,net->second.observed,onlineTtl);
        if(netValid && (!rfValid || (now-net->second.observed<=onlineFresh && net->second.observed>=rfObserved))) {
            p=net->second;have=true;source="adsb_lol";++onlineCount;
        } else if(rfValid) {
            p.lat=m.latDeg;p.lon=m.lonDeg;p.alt=m.altitudeFt;p.hasAltitude=std::isfinite(p.alt);
            p.observed=rfObserved;
            p.motion=m.hasGroundVector && std::isfinite(m.groundTrackDeg) && std::isfinite(m.groundSpeedKnots) &&
                m.groundTrackDeg>=0 && m.groundTrackDeg<360 && m.groundSpeedKnots>=0 && m.groundSpeedKnots<=1200;
            p.track=m.groundTrackDeg;p.speed=m.groundSpeedKnots;source="rf_adsc";have=true;++rfCount;
        }
        if(!have) {
            ++unlocated;reason=m.hasPosition?"position_expired":!m.classicAeroIdentity?"identity_unverified":
                !enabled_?"online_disabled":"awaiting_position";
        }
        nlohmann::json row={{"aesId",id},{"icaoHex",m.icaoHex},{"registration",m.registration},{"callsign",m.callsign},
            {"identitySource",m.classicAeroIdentity?"classic_aero_address":"unspecified"},
            {"lastSeenAgeSeconds",now-a.lastSeenMonotonic},{"messages",a.messages},{"reason",reason},{"voiceActive",voices.contains(id)}};
        if(have) {
            if(voices.contains(id))--unmappedVoice;
            const double age=std::max(0.0,now-p.observed);
            const bool isStale=age>(source=="rf_adsc"?300:onlineFresh);
            row["reason"]=isStale?"stale_position":"position_available";
            row["positionSource"]=source;row["ageSeconds"]=age;row["stale"]=isStale;
            row["measuredLatDeg"]=p.lat;row["measuredLonDeg"]=p.lon;
            row["estimated"]=false;row["hasAltitude"]=p.hasAltitude;
            if(p.hasAltitude)row["altitudeFt"]=p.alt;
            if(p.motion) {row["groundTrackDeg"]=p.track;row["groundSpeedKnots"]=p.speed;}
            // DEC-0164: short spherical forward projection, labelled explicitly.
            // Never add an estimated sample to either measured-source cache.
            if(estimates && p.motion && age>=10 && age<=predictionLimit) {
                constexpr double rad=std::numbers::pi/180.0;
                const double seconds=std::floor(age/10)*10;
                const double d=p.speed*(1852.0/3600)*seconds/6371008.8;
                const double lat=p.lat*rad,lon=p.lon*rad,b=p.track*rad;
                const double projected=std::asin(std::clamp(std::sin(lat)*std::cos(d)+std::cos(lat)*std::sin(d)*std::cos(b),-1.0,1.0));
                p.lon=std::remainder((lon+std::atan2(std::sin(b)*std::sin(d)*std::cos(lat),std::cos(d)-std::sin(lat)*std::sin(projected)))/rad,360.0);
                p.lat=projected/rad;row["estimated"]=true;row["estimateSeconds"]=seconds;++estimateCount;
            }
            row["latDeg"]=p.lat;row["lonDeg"]=p.lon;
            if(isStale)++stale;
            result["positions"].push_back(row);
        }
        result["aircraft"].push_back(std::move(row));
    }
    auto diagnostic=counters();
    diagnostic.update({{"mapRfPositions",rfCount},{"mapOnlinePositions",onlineCount},{"mapEstimatedPositions",estimateCount},
        {"mapUnlocated",unlocated},{"mapStale",stale},{"mapVoiceWithoutPosition",unmappedVoice},
        {"mapActiveCalls",voices.size()},{"mapAircraft",result["aircraft"].size()},{"mapVoiceWithoutIdentity",unknownVoice}});
    result["tracking"]=diagnostic;
    return result;
}
