#include "arxml_parser.h"

#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace usde {

namespace {

inline std::string_view LocalName(std::string_view name) {
    if (name.size() > 3 && name[0] == 'A' && name[1] == 'R' && name[2] == ':')
        return name.substr(3);
    return name;
}

inline std::string PathBaseName(std::string_view path) {
    auto pos = path.rfind('/');
    if (pos == std::string_view::npos) return std::string(path);
    return std::string(path.substr(pos + 1));
}

static std::string ExtractTagContent(const std::string& xml,
                                     const std::string& tag,
                                     size_t start = 0) {
    std::string open = "<" + tag;
    auto o1 = xml.find(open, start);
    if (o1 == std::string::npos) return {};
    auto gt = xml.find('>', o1 + open.size());
    if (gt == std::string::npos) return {};
    if (xml[gt - 1] == '/') return {};
    std::string close = "</" + tag + ">";
    auto c1 = xml.find(close, gt + 1);
    if (c1 == std::string::npos) return {};
    auto a = xml.find_first_not_of(" \t\r\n", gt + 1);
    if (a == std::string::npos || a >= c1) return {};
    auto b = xml.find_last_not_of(" \t\r\n", c1 - 1);
    if (b == std::string::npos || b < a) return {};
    return xml.substr(a, b - a + 1);
}

static size_t FindOpenTag(const std::string& xml,
                          const std::string& tag,
                          size_t start = 0) {
    std::string prefix = "<" + tag;
    while (true) {
        auto pos = xml.find(prefix, start);
        if (pos == std::string::npos) return pos;
        auto after = pos + prefix.size();
        if (after >= xml.size()) return pos;
        char c = xml[after];
        if (c == '>' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '/')
            return pos;
        start = pos + 1;
    }
}

static size_t FindCloseTag(const std::string& xml,
                           const std::string& tag,
                           size_t open_pos) {
    std::string close = "</" + tag + ">";
    auto pos = xml.find(close, open_pos);
    if (pos == std::string::npos) return xml.size();
    return pos + close.size();
}

} // anonymous namespace

bool LoadARXML(const std::filesystem::path& file_path,
               NetworkCluster&             out_cluster) {
    std::ifstream ifs(file_path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) return false;
    std::string xml((std::istreambuf_iterator<char>(ifs)),
                     std::istreambuf_iterator<char>());
    ifs.close();

    // ── 1. Extract I-SIGNAL definitions (name -> bit_length) ────────────
    std::unordered_map<std::string, uint32_t> signal_lengths;
    {
        size_t pos = 0;
        while (true) {
            auto open = FindOpenTag(xml, "I-SIGNAL", pos);
            if (open == std::string::npos) break;
            auto close = FindCloseTag(xml, "I-SIGNAL", open);
            auto name = ExtractTagContent(xml, "SHORT-NAME", open);
            if (!name.empty()) {
                auto name_pos = xml.find("<SHORT-NAME>", open);
                if (name_pos != std::string::npos && name_pos < close) {
                    auto len = ExtractTagContent(xml, "LENGTH", open);
                    if (!len.empty())
                        signal_lengths[name] = static_cast<uint32_t>(std::stoul(len));
                }
            }
            pos = close;
        }
    }

    // ── 2. Extract PDU definitions (name, length, signal mappings) ──────
    struct PduDef {
        std::string name;
        uint32_t    header_id   = 0;
        uint32_t    byte_length = 0;
    };
    std::unordered_map<std::string, PduDef> pdu_defs;

    struct MappingEntry {
        std::string signal_name;
        std::string pdu_name;
        uint32_t    start_pos  = 0;
        ByteOrder   byte_order = ByteOrder::INTEL;
    };
    std::vector<MappingEntry> mappings;

    auto processPduElement = [&](const std::string& pdu_tag) {
        size_t pos = 0;
        while (true) {
            auto open = FindOpenTag(xml, pdu_tag, pos);
            if (open == std::string::npos) break;
            auto close = FindCloseTag(xml, pdu_tag, open);

            auto pdu_name = ExtractTagContent(xml, "SHORT-NAME", open);
            if (pdu_name.empty() || pdu_name.size() > 200) { pos = close; continue; }

            PduDef pd;
            pd.name = pdu_name;
            auto hid = ExtractTagContent(xml, "HEADER-ID-SHORT-HEADER", open);
            if (!hid.empty())
                pd.header_id = static_cast<uint32_t>(std::stoul(hid));
            auto plen = ExtractTagContent(xml, "LENGTH", open);
            if (!plen.empty())
                pd.byte_length = static_cast<uint32_t>(std::stoul(plen));
            pdu_defs[pdu_name] = pd;

            size_t mpos = open;
            while (mpos < close) {
                auto mopen = FindOpenTag(xml, "I-SIGNAL-TO-I-PDU-MAPPING", mpos);
                if (mopen == std::string::npos || mopen >= close) break;
                auto mclose = FindCloseTag(xml, "I-SIGNAL-TO-I-PDU-MAPPING", mopen);

                MappingEntry me;
                me.pdu_name = pdu_name;
                auto sr = ExtractTagContent(xml, "I-SIGNAL-REF", mopen);
                if (!sr.empty()) me.signal_name = PathBaseName(sr);
                auto sp = ExtractTagContent(xml, "START-POSITION", mopen);
                if (!sp.empty()) me.start_pos = static_cast<uint32_t>(std::stoul(sp));
                auto bo = ExtractTagContent(xml, "PACKING-BYTE-ORDER", mopen);
                if (!bo.empty())
                    me.byte_order = (bo.find("FIRST") != std::string::npos)
                                        ? ByteOrder::MOTOROLA : ByteOrder::INTEL;
                if (!me.signal_name.empty()) mappings.push_back(std::move(me));
                mpos = mclose;
            }
            pos = close;
        }
    };
    processPduElement("NM-PDU");
    processPduElement("I-PDU");
    processPduElement("SECURED-I-PDU");
    processPduElement("CONTAINER-I-PDU");

    // ── 3. Extract CAN-FRAME definitions & PDU-TO-FRAME-MAPPING ─────────
    struct FrameDef {
        std::string name;
        uint32_t    frame_len = 8;
    };
    std::unordered_map<std::string, FrameDef> frame_defs;
    std::unordered_map<std::string, std::string> pdu_to_frame;
    std::unordered_map<std::string, uint32_t>    pdu_start_pos;   // pdu_name -> start byte in frame
    {
        size_t pos = 0;
        while (true) {
            auto open = FindOpenTag(xml, "CAN-FRAME", pos);
            if (open == std::string::npos) break;
            auto close = FindCloseTag(xml, "CAN-FRAME", open);
            auto name = ExtractTagContent(xml, "SHORT-NAME", open);
            if (name.empty()) { pos = close; continue; }
            FrameDef fd; fd.name = name;
            auto fl = ExtractTagContent(xml, "FRAME-LENGTH", open);
            if (!fl.empty()) fd.frame_len = static_cast<uint32_t>(std::stoul(fl));
            frame_defs[name] = fd;
            size_t mpos = open;
            while (mpos < close) {
                auto mopen = FindOpenTag(xml, "PDU-TO-FRAME-MAPPING", mpos);
                if (mopen == std::string::npos || mopen >= close) break;
                auto mclose = FindCloseTag(xml, "PDU-TO-FRAME-MAPPING", mopen);
                auto pref = ExtractTagContent(xml, "PDU-REF", mopen);
                if (!pref.empty()) {
                    auto pn = PathBaseName(pref);
                    pdu_to_frame[pn] = name;
                    auto sp = ExtractTagContent(xml, "START-POSITION", mopen);
                    if (!sp.empty()) pdu_start_pos[pn] = static_cast<uint32_t>(std::stoul(sp));
                }
                mpos = mclose;
            }
            pos = close;
        }
    }

    // ── 4. Parse CAN-CLUSTER topology → clusters + CAN IDs ──────────────
    std::unordered_map<std::string, uint32_t> frame_can_ids;
    std::unordered_map<std::string, std::string> frame_cluster; // frame-name → cluster-name

    // Helper: extract frame ID + cluster from a triggering element
    auto parseTriggering = [&](size_t open, size_t close, const std::string& cluster_name) {
        auto frame_ref = ExtractTagContent(xml, "FRAME-REF", open);
        auto identifier = ExtractTagContent(xml, "IDENTIFIER", open);
        if (!frame_ref.empty() && !identifier.empty()) {
            std::string fname = PathBaseName(frame_ref);
            frame_can_ids[fname] = static_cast<uint32_t>(std::stoul(identifier));
            if (!cluster_name.empty()) frame_cluster[fname] = cluster_name;
        }
    };

    // Parse CAN-CLUSTER elements
    {
        size_t pos = 0;
        while (true) {
            auto open = FindOpenTag(xml, "CAN-CLUSTER", pos);
            if (open == std::string::npos) break;
            auto close = FindCloseTag(xml, "CAN-CLUSTER", open);

            // Read cluster metadata
            Cluster cl;
            cl.name = ExtractTagContent(xml, "SHORT-NAME", open);
            auto baud = ExtractTagContent(xml, "BAUDRATE", open);
            if (!baud.empty()) cl.baudrate = static_cast<uint32_t>(std::stoul(baud));
            // Check CAN-FD support
            auto fd = ExtractTagContent(xml, "CAN-FD-FRAME-SUPPORT", open);
            auto tx = ExtractTagContent(xml, "CAN-FRAME-TX-BEHAVIOR", open);
            cl.can_fd = (fd == "true") || (tx == "CAN-FD");
            cl.bus_type = cl.can_fd ? "CAN-FD" : "CAN";

            // Parse frame triggerings within this cluster
            size_t tpos = open;
            while (tpos < close) {
                auto to = FindOpenTag(xml, "CAN-FRAME-TRIGGERING", tpos);
                if (to == std::string::npos || to >= close) break;
                auto tc = FindCloseTag(xml, "CAN-FRAME-TRIGGERING", to);
                parseTriggering(to, tc, cl.name);
                tpos = tc;
            }

            out_cluster.clusters[cl.name] = std::move(cl);
            pos = close;
        }
    }

    // ── 5. Build output ─────────────────────────────────────────────────
    std::unordered_map<std::string, std::vector<const MappingEntry*>> pdu_sigs;
    for (auto& m : mappings) pdu_sigs[m.pdu_name].push_back(&m);

    for (auto& [pdu_name, sig_ptrs] : pdu_sigs) {
        // Resolve PDU → Frame name
        std::string frame_name = pdu_name;
        auto ptf_it = pdu_to_frame.find(pdu_name);
        if (ptf_it != pdu_to_frame.end()) frame_name = ptf_it->second;

        // Get or create Frame
        uint32_t fid = 0;
        auto id_it = frame_can_ids.find(frame_name);
        if (id_it != frame_can_ids.end()) fid = id_it->second;
        else fid = static_cast<uint32_t>(std::hash<std::string>{}(frame_name) & 0x7FFFFFFF);

        // Try to find existing frame or create new
        auto fit = out_cluster.frames.find(fid);
        if (fit == out_cluster.frames.end()) {
            Frame f;
            f.id = fid;
            f.name = frame_name;
            auto fd = frame_defs.find(frame_name);
            if (fd != frame_defs.end()) f.dlc = fd->second.frame_len;
            out_cluster.frames[fid] = std::move(f);
            fit = out_cluster.frames.find(fid);
        }

        // Build PDU
        Pdu pdu;
        pdu.name = pdu_name;
        auto pi = pdu_defs.find(pdu_name);
        if (pi != pdu_defs.end()) {
            pdu.byte_length = pi->second.byte_length;
            pdu.header_id   = pi->second.header_id;
        }
        auto psp = pdu_start_pos.find(pdu_name);
        if (psp != pdu_start_pos.end()) pdu.start_position = psp->second;

        for (auto* mp : sig_ptrs) {
            Signal sig;
            sig.name       = mp->signal_name;
            sig.start_bit  = mp->start_pos;
            sig.byte_order = mp->byte_order;
            sig.factor     = 1.0;
            sig.offset     = 0.0;
            auto li = signal_lengths.find(mp->signal_name);
            if (li != signal_lengths.end()) sig.bit_length = li->second;

            pdu.signals.push_back(sig);
            // Also add to frame.signals for codec compatibility
            fit->second.signals.push_back(sig);
        }

        fit->second.pdus.push_back(std::move(pdu));
    }

    // Assign frames to clusters
    for (auto& [cl_name, cluster] : out_cluster.clusters) {
        for (auto& [fc_name, cl] : frame_cluster) {
            if (cl == cl_name) {
                auto id_it = frame_can_ids.find(fc_name);
                if (id_it != frame_can_ids.end()) {
                    auto fit = out_cluster.frames.find(id_it->second);
                    if (fit != out_cluster.frames.end())
                        cluster.frames[id_it->second] = fit->second;
                }
            }
        }
    }

    return true;
}

} // namespace usde
