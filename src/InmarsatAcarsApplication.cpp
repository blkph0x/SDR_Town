#include "InmarsatAcarsApplication.h"
#include <libacars/miam-core.h>
#include <libacars/acars.h>
#include <libacars/adsc.h>
#include <libacars/arinc.h>
#include "decoder_status.h"
#include <libacars/miam.h>
#include <libacars/ohma.h>
#include <libacars/media-adv.h>
#include <libacars/vstring.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <QString>
#include <cmath>
#include <algorithm>

namespace {
// libacars v2.2.1 accepts incomplete/overflowing base85 groups. Validate before
// entering it, including decoded length vs padding. DEC-0161, MIAM CORE syntax.
bool validBase85(std::string_view input, unsigned padding) {
    size_t bytes = 0;
    unsigned digits = 0;
    uint64_t value = 0;
    for (unsigned char c : input) {
        if (c == 'z' && digits == 0) { bytes += 4; continue; }
        if (c < '!' || c > 'u') return false;
        value = value * 85 + (c - '!');
        if (++digits == 5) {
            if (value > UINT32_MAX) return false;
            bytes += 4; digits = 0; value = 0;
        }
    }
    return digits == 0 && bytes >= padding;
}

uint32_t errors(const la_proto_node* node) {
    if (node->td == &la_DEF_arinc_message) return !static_cast<const la_arinc_msg*>(node->data)->crc_ok;
    if (node->td == &la_DEF_adsc_message) return static_cast<const la_adsc_msg_t*>(node->data)->err;
    if (sdr_acars_cpdlc_error(node)) return 1;
    if (node->td == &la_DEF_ohma_msg) return static_cast<const la_ohma_msg*>(node->data)->err;
    if (node->td == &la_DEF_media_adv_message) return static_cast<const la_media_adv_msg*>(node->data)->err;
    if (node->td == &la_DEF_miam_core_pdu) return static_cast<const la_miam_core_pdu*>(node->data)->err;
    if (node->td == &la_DEF_miam_core_v1_data_pdu) return static_cast<const la_miam_core_v1_data_pdu*>(node->data)->err;
    if (node->td == &la_DEF_miam_core_v2_data_pdu) return static_cast<const la_miam_core_v2_data_pdu*>(node->data)->err;
    if (node->td == &la_DEF_miam_core_v1_ack_pdu) return static_cast<const la_miam_core_v1_ack_pdu*>(node->data)->err;
    if (node->td == &la_DEF_miam_core_v2_ack_pdu) return static_cast<const la_miam_core_v2_ack_pdu*>(node->data)->err;
    if (node->td == &la_DEF_miam_core_v1v2_alo_pdu || node->td == &la_DEF_miam_core_v1v2_alr_pdu)
        return static_cast<const la_miam_core_v1v2_alo_alr_pdu*>(node->data)->err;
    return 0;
}

bool validCore(std::string_view core) {
    if (core.size() < 4) return false;
    const char bpad = core[0], hpad = core[1];
    const auto delim = core.find('|', 2);
    if (delim == std::string_view::npos || delim == 2 || hpad < '0' || hpad > '3' ||
        !((bpad >= '0' && bpad <= '3') || bpad == '.' || bpad == '-')) return false;
    if (!validBase85(core.substr(2, delim - 2), hpad - '0')) return false;
    const auto body = core.substr(delim + 1);
    if (bpad >= '0' && bpad <= '3' && !validBase85(body, bpad - '0')) return false;
    return bpad != '.' || body.empty();
}

// Direction and ARINC CRC are checked before walking typed downlink groups.
// In particular an uplink contract's tag 7 is NOT a basic position report.
void aircraftFields(InmarsatAcarsApplication& result, la_proto_node* root) {
    const auto* arincNode = la_proto_tree_find_arinc(root);
    const auto* adscNode = la_proto_tree_find_adsc(root);
    if (!arincNode || !adscNode) return;
    const auto* arinc = static_cast<const la_arinc_msg*>(arincNode->data);
    const auto* adsc = static_cast<const la_adsc_msg_t*>(adscNode->data);
    if (!arinc->crc_ok || adsc->err) return;
    InmarsatAdscPosition p;
    p.registration = arinc->air_reg;
    p.registration.erase(0, p.registration.find_first_not_of('.'));
    bool hasPosition = false, haveIdentity = false;
    for (const auto* item = adsc->tag_list; item; item = item->next) {
        const auto* tag = static_cast<const la_adsc_tag_t*>(item->data);
        if (!tag || !tag->data) continue;
        if (tag->tag == 7 || tag->tag == 9 || tag->tag == 10 ||
            tag->tag == 18 || tag->tag == 19 || tag->tag == 20) {
            const auto* report = static_cast<const la_adsc_basic_report_t*>(tag->data);
            // Reject ambiguous multiple reports rather than select a convenient one.
            if (hasPosition || !std::isfinite(report->lat) || !std::isfinite(report->lon) ||
                std::abs(report->lat) > 90 || std::abs(report->lon) > 180 ||
                !std::isfinite(report->timestamp) || report->timestamp < 0 || report->timestamp >= 3600) return;
            p.latitude = report->lat; p.longitude = report->lon;
            p.altitudeFt = report->alt; p.secondsPastHour = report->timestamp;
            hasPosition = true;
        } else if (tag->tag == 12) {
            const auto* id = static_cast<const la_adsc_flight_id_t*>(tag->data);
            p.callsign = id->id;
            while (!p.callsign.empty() && p.callsign.back() == ' ') p.callsign.pop_back();
            haveIdentity = true;
        } else if (tag->tag == 14) {
            // libacars adsc.c earth_ref: true track degrees / ground speed knots.
            // Tag 15 is heading/Mach and must never drive map extrapolation.
            const auto* vector = static_cast<const la_adsc_earth_air_ref_t*>(tag->data);
            if (!vector->heading_invalid && std::isfinite(vector->heading) &&
                std::isfinite(vector->speed) && vector->speed >= 0 && vector->speed <= 1200) {
                p.hasGroundVector = true;
                p.groundTrackDeg = std::fmod(vector->heading + 360.0, 360.0);
                p.groundSpeedKnots = vector->speed;
            }
        } else if (tag->tag == 17) {
            const auto* id = static_cast<const la_adsc_airframe_id_t*>(tag->data);
            const auto icao = uint32_t(id->icao_hex[0]) << 16 | uint32_t(id->icao_hex[1]) << 8 | id->icao_hex[2];
            if (p.airframeId && p.airframeId != icao) return;
            p.airframeId = icao; haveIdentity = true;
        }
    }
    if (hasPosition || haveIdentity) { result.aircraft = p; result.hasPosition = hasPosition; }
}
}

InmarsatAcarsApplication decodeInmarsatAcarsApplication(const std::string& label, const std::string& raw,
    InmarsatMessageDirection direction, bool includesDownlinkHeader) {
    if (label == std::string("_\x7f", 2) && raw.empty())
        return {"ACARS ACK", "control", "ACARS acknowledgement (no text payload)"};
    InmarsatAcarsApplication result{"ACARS", "uninterpreted", QString::fromUtf8(raw.data(),
        static_cast<qsizetype>(std::min<size_t>(raw.size(), 16384))).toUtf8().toStdString()};
    if(label=="MA") result={"MIAM","unsupported","Unsupported or incomplete MIAM transfer"};
    // Limits are application resource budgets, not RF thresholds.
    if (raw.size() > 16384 || raw.find('\0') != std::string::npos || label.size() != 2) {
        result.status = "invalid"; result.text = "Invalid or oversized ACARS application payload"; return result;
    }
    const auto dir = direction == InmarsatMessageDirection::AirToGround ? LA_MSG_DIR_AIR2GND :
        direction == InmarsatMessageDirection::GroundToAir ? LA_MSG_DIR_GND2AIR : LA_MSG_DIR_UNKNOWN;
    static std::mutex mutex; // libacars configuration/CRC tables are lazily initialized.
    std::lock_guard lock(mutex);
    std::string payload = raw;
    // JAERO's reassembled message retains the 4-byte message number and 6-byte
    // flight ID. Strip ONLY when the caller supplies validated downlink framing.
    // Same contract as libacars acars.c::IS_DOWNLINK_BLK / parse_and_reassemble.
    if (includesDownlinkHeader) {
        if (direction != InmarsatMessageDirection::AirToGround || payload.size() < 10)
            return {"ACARS", "invalid", "Incomplete downlink message header"};
        payload.erase(0, 10);
    }
    while (!payload.empty() && (payload.back() == '\r' || payload.back() == '\n')) payload.pop_back();
    const int offset = la_acars_extract_sublabel_and_mfi(label.c_str(), dir, payload.c_str(),
        static_cast<int>(payload.size()), nullptr, nullptr);
    if (offset > 0 && size_t(offset) <= payload.size()) payload.erase(0, offset);
    if ((label == "MA" || label == "H1") && !payload.empty()) {
        if (payload[0] == 'T' && !validCore(std::string_view(payload).substr(1))) {
            const bool signature=payload.size()>=3 && (payload[1]=='.' || payload[1]=='-' ||
                (payload[1]>='0' && payload[1]<='3')) && payload[2]>='0' && payload[2]<='3';
            if(label=="MA" || signature)
                return {"MIAM", "invalid", "MIAM single transfer: invalid or incomplete encoding"};
            return result; // ordinary H1 prose starting with T is not MIAM evidence
        }
        // Upstream's stateless segment parser tries to decode a fragment as a
        // complete core. Do not enter it without bounded application reassembly.
        if (payload[0]=='S' && (label=="MA" || (payload.size()>=7 &&
            std::all_of(payload.begin()+1,payload.begin()+7,[](char c){return c>='0' && c<='9';}))))
            return {"MIAM", "unsupported", "MIAM file segment: application reassembly is not enabled"};
    }
    // Upstream ARINC assumes complete hex before stripping its two-byte CRC;
    // prevent truncated/non-hex lengths and unknown-direction ASN.1 dispatch.
    if (label == "H1" || label == "A6" || label == "AA" || label == "B6" || label == "BA") {
        for (const auto* imi : {".AT1", ".CR1", ".CC1", ".DR1", ".ADS", ".DIS"}) {
            const auto at = payload.find(imi);
            if (at == std::string::npos) continue;
            if (dir == LA_MSG_DIR_UNKNOWN)
                return {"ARINC 622", "unsupported", "Message direction is unknown; no position inferred"};
            if (payload.size() < at + 15)
                return {"ARINC 622", "invalid", "Truncated ARINC 622 application"};
            const auto hex = std::string_view(payload).substr(at + 11);
            if (hex.size() % 2 || !std::all_of(hex.begin(), hex.end(), [](unsigned char c) {
                return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
            })) return {"ARINC 622", "invalid", "Invalid ARINC 622 hexadecimal payload"};
            break;
        }
    }
    std::unique_ptr<la_proto_node, decltype(&la_proto_tree_destroy)> tree(
        la_acars_decode_apps(label.c_str(), payload.c_str(), dir), la_proto_tree_destroy);
    if (!tree) return result;
    uint32_t err = 0;
    for (auto* node = tree.get(); node; node = node->next) err |= errors(node);
    result.status = err ? "invalid" : "decoded";
    result.protocol = "ACARS application";
    if (la_proto_tree_find_arinc(tree.get())) result.protocol = "ARINC 622";
    if (la_proto_tree_find_adsc(tree.get())) result.protocol = "ADS-C";
    if (sdr_acars_has_cpdlc(tree.get())) result.protocol = "CPDLC";
    if (la_proto_tree_find_miam(tree.get())) {
        result.protocol = "MIAM";
        if (!err && !la_proto_tree_find_protocol(tree.get(), &la_DEF_miam_core_pdu)) result.status = "control";
        if (!err && la_proto_tree_find_protocol(tree.get(), &la_DEF_miam_file_segment_message)) result.status = "unsupported";
    }
    if (la_proto_tree_find_ohma(tree.get())) {
        result.protocol = "OHMA";
        const auto* ohma = static_cast<const la_ohma_msg*>(la_proto_tree_find_ohma(tree.get())->data);
        if (!err && ohma->msg_seq > 0 && ohma->msg_total != 1) result.status = "unsupported";
    }
    if (tree->td == &la_DEF_media_adv_message) result.protocol = "Media advisory";
    auto destroyText = [](la_vstring* text) { la_vstring_destroy(text, true); };
    std::unique_ptr<la_vstring, decltype(destroyText)> text(
        la_proto_tree_format_text(nullptr, tree.get()), destroyText);
    // RF content must not make JSON serialization fail on non-UTF8 bytes.
    // Raw ACARS remains separate; only the human-facing rendition is sanitized.
    if (text && text->str) result.text = QString::fromUtf8(text->str).left(16384).toUtf8().toStdString();
    if (result.status == "unsupported") result.text += "\nMultipart application reassembly is not enabled";
    if (!err && direction == InmarsatMessageDirection::AirToGround) aircraftFields(result, tree.get());
    return result;
}
