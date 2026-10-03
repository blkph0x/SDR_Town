#pragma once
#include "DtmfDecoder.h"
#include <nlohmann/json.hpp>

inline nlohmann::json dtmfOptionsJson(const DtmfOptions& o) {
    return {{"profile",o.fast?"fast":"conservative"},{"inverted",o.inverted},
        {"inversionHz",o.inversionHz},{"pitchScale",o.pitchScale},{"shiftHz",o.shiftHz}};
}
inline nlohmann::json dtmfReport(const DtmfSnapshot& s) {
    auto history=nlohmann::json::array();
    for (const auto& e:s.history) history.push_back({{"digit",std::string(1,e.digit)},
        {"epoch",e.epoch},{"firstSample",e.firstSample},{"confirmedSample",e.confirmedSample},
        {"sampleRate",e.rate},{"targetHz",e.targetHz},{"rowHz",e.rowHz},{"columnHz",e.columnHz},
        {"purity",e.purity},{"twistDb",e.twistDb},{"options",dtmfOptionsJson(e.options)}});
    return {{"decoder","dtmf"},{"schema",1},{"digit",s.digit?std::string(1,s.digit):""},
        {"sequence",s.sequence},{"lastSequence",s.lastSequence},{"samples",s.samples},
        {"frames",s.frames},{"sampleRate",s.sampleRate},{"targetHz",s.targetHz},
        {"confirmedDigits",s.confirmedDigits},{"purity",s.purity},{"twistDb",s.twistDb},
        {"status",s.status},{"rejection",s.rejection},{"resets",s.resets},
        {"rejectedFrames",s.rejectedFrames},{"droppedEvents",s.droppedEvents},
        {"truncatedDigits",s.truncatedDigits},{"processingUs",s.processingUs},
        {"options",dtmfOptionsJson(s.options)},{"detections",history}};
}
