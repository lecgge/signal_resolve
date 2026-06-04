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

// Extract text content between <TAG> and </TAG> (or <TAG ...> and </TAG>).
// Returns empty string if not found.
static std::string ExtractTagContent(const std::string& xml,
                                     const std::string& tag,
                                     size_t start = 0) {
    std::string open = "<" + tag;
    auto o1 = xml.find(open, start);
    if (o1 == std::string::npos) return {};
    auto gt = xml.find('>', o1 + open.size());
    if (gt == std::string::npos) return {};
    // Skip self-closing
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

// Find the next occurrence of <TAG> or <TAG ...> and return the position
// of the '<'.  Returns npos if not found.
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
        // Must be followed by '>', ' ', '\t', '\n', '\r', or '/'
        if (c == '>' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '/')
            return pos;
        start = pos + 1;
    }
}

// Find the matching </TAG> for a <TAG ...> at position |open_pos|.
// Returns the position after </TAG>.
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
    // Read entire file into memory (28MB is fine for modern systems)
    std::ifstream ifs(file_path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) return false;
    std::string xml((std::istreambuf_iterator<char>(ifs)),
                     std::istreambuf_iterator<char>());
    ifs.close();

    // ── 1. Extract I-SIGNAL definitions ─────────────────────────────────
    std::unordered_map<std::string, uint32_t> signal_lengths;
    {
        const std::string tag = "I-SIGNAL";
        size_t pos = 0;
        while (true) {
            auto open = FindOpenTag(xml, tag, pos);
            if (open == std::string::npos) break;
            auto close = FindCloseTag(xml, tag, open);
            std::string_view block(&xml[open], close - open);

            auto name = ExtractTagContent(xml, "SHORT-NAME", open);
            if (!name.empty()) {
                // Only process if SHORT-NAME is inside this I-SIGNAL block
                auto name_pos = xml.find("<SHORT-NAME>", open);
                if (name_pos != std::string::npos && name_pos < close) {
                    auto len = ExtractTagContent(xml, "LENGTH", open);
                    if (!len.empty()) {
                        uint32_t bit_len = static_cast<uint32_t>(std::stoul(len));
                        signal_lengths[name] = bit_len;
                    }
                }
            }
            pos = close;
        }
    }

    // ── 2. Extract I-SIGNAL-TO-I-PDU-MAPPING entries ────────────────────
    //    These are inside PDU elements (NM-PDU, I-PDU, etc.)
    struct MappingEntry {
        std::string signal_name;
        std::string pdu_name;
        uint32_t    start_pos = 0;
        ByteOrder   byte_order = ByteOrder::INTEL;
    };
    std::vector<MappingEntry> mappings;

    // Find all PDU elements and extract their mappings
    auto processPduElement = [&](const std::string& pdu_tag) {
        size_t pos = 0;
        while (true) {
            auto open = FindOpenTag(xml, pdu_tag, pos);
            if (open == std::string::npos) break;
            auto close = FindCloseTag(xml, pdu_tag, open);

            // Get PDU's SHORT-NAME
            auto pdu_name = ExtractTagContent(xml, "SHORT-NAME", open);
            if (pdu_name.empty() || pdu_name.size() > 200) {
                pos = close;
                continue;
            }

            // Find all I-SIGNAL-TO-I-PDU-MAPPING inside this PDU
            size_t mpos = open;
            while (mpos < close) {
                auto mopen = FindOpenTag(xml, "I-SIGNAL-TO-I-PDU-MAPPING", mpos);
                if (mopen == std::string::npos || mopen >= close) break;
                auto mclose = FindCloseTag(xml, "I-SIGNAL-TO-I-PDU-MAPPING", mopen);

                MappingEntry me;
                me.pdu_name = pdu_name;

                auto sig_ref = ExtractTagContent(xml, "I-SIGNAL-REF", mopen);
                if (!sig_ref.empty())
                    me.signal_name = PathBaseName(sig_ref);

                auto start_pos = ExtractTagContent(xml, "START-POSITION", mopen);
                if (!start_pos.empty())
                    me.start_pos = static_cast<uint32_t>(std::stoul(start_pos));

                auto byte_order = ExtractTagContent(xml, "PACKING-BYTE-ORDER", mopen);
                if (!byte_order.empty())
                    me.byte_order = (byte_order.find("FIRST") != std::string::npos)
                                        ? ByteOrder::MOTOROLA : ByteOrder::INTEL;

                if (!me.signal_name.empty())
                    mappings.push_back(std::move(me));

                mpos = mclose;
            }
            pos = close;
        }
    };

    processPduElement("NM-PDU");
    processPduElement("I-PDU");
    processPduElement("SECURED-I-PDU");
    processPduElement("CONTAINER-I-PDU");

    // ── 3. Extract CAN-FRAME definitions ────────────────────────────────
    struct FrameDef {
        std::string name;
        uint32_t    frame_len = 8;
    };
    std::unordered_map<std::string, FrameDef> frame_defs;
    std::unordered_map<std::string, std::string> pdu_to_frame; // pdu-name -> frame-name
    {
        size_t pos = 0;
        while (true) {
            auto open = FindOpenTag(xml, "CAN-FRAME", pos);
            if (open == std::string::npos) break;
            auto close = FindCloseTag(xml, "CAN-FRAME", open);

            auto name = ExtractTagContent(xml, "SHORT-NAME", open);
            if (name.empty()) { pos = close; continue; }

            FrameDef fd;
            fd.name = name;
            auto fl = ExtractTagContent(xml, "FRAME-LENGTH", open);
            if (!fl.empty())
                fd.frame_len = static_cast<uint32_t>(std::stoul(fl));

            frame_defs[name] = fd;

            // Extract PDU-TO-FRAME-MAPPING entries inside this CAN-FRAME
            size_t mpos = open;
            while (mpos < close) {
                auto mopen = FindOpenTag(xml, "PDU-TO-FRAME-MAPPING", mpos);
                if (mopen == std::string::npos || mopen >= close) break;
                auto mclose = FindCloseTag(xml, "PDU-TO-FRAME-MAPPING", mopen);
                auto pdu_ref = ExtractTagContent(xml, "PDU-REF", mopen);
                if (!pdu_ref.empty()) {
                    std::string pdu_name = PathBaseName(pdu_ref);
                    pdu_to_frame[pdu_name] = name;
                }
                mpos = mclose;
            }

            pos = close;
        }
    }

    // ── 4. Extract CAN-FRAME-TRIGGERING (CAN ID mapping) ────────────────
    std::unordered_map<std::string, uint32_t> frame_can_ids;
    {
        size_t pos = 0;
        while (true) {
            auto open = FindOpenTag(xml, "CAN-FRAME-TRIGGERING", pos);
            if (open == std::string::npos) break;
            auto close = FindCloseTag(xml, "CAN-FRAME-TRIGGERING", open);

            auto frame_ref = ExtractTagContent(xml, "FRAME-REF", open);
            auto identifier = ExtractTagContent(xml, "IDENTIFIER", open);
            if (!frame_ref.empty() && !identifier.empty()) {
                std::string fname = PathBaseName(frame_ref);
                frame_can_ids[fname] = static_cast<uint32_t>(std::stoul(identifier));
            }
            pos = close;
        }
    }

    // ── 5. Build output ─────────────────────────────────────────────────
    // Group mappings by PDU
    std::unordered_map<std::string, std::vector<const MappingEntry*>> pdu_sigs;
    for (auto& m : mappings)
        pdu_sigs[m.pdu_name].push_back(&m);

    for (auto& [pdu_name, sig_ptrs] : pdu_sigs) {
        Frame frame;
        frame.name = pdu_name;

        // Resolve PDU -> CAN-FRAME name via PDU-TO-FRAME-MAPPING
        std::string frame_name = pdu_name;
        auto ptf_it = pdu_to_frame.find(pdu_name);
        if (ptf_it != pdu_to_frame.end())
            frame_name = ptf_it->second;

        // Look up CAN ID from CAN-FRAME-TRIGGERING
        auto id_it = frame_can_ids.find(frame_name);
        if (id_it != frame_can_ids.end()) {
            frame.id = id_it->second;
        } else {
            frame.id = static_cast<uint32_t>(
                std::hash<std::string>{}(frame_name) & 0x7FFFFFFF);
        }

        // Look up frame length from CAN-FRAME definition
        auto fd_it = frame_defs.find(frame_name);
        if (fd_it != frame_defs.end())
            frame.dlc = fd_it->second.frame_len;

        for (auto* mp : sig_ptrs) {
            Signal sig;
            sig.name       = mp->signal_name;
            sig.start_bit  = mp->start_pos;
            sig.byte_order = mp->byte_order;
            sig.factor     = 1.0;
            sig.offset     = 0.0;
            auto len_it = signal_lengths.find(mp->signal_name);
            if (len_it != signal_lengths.end())
                sig.bit_length = len_it->second;
            frame.signals.push_back(std::move(sig));
        }

        if (!frame.signals.empty())
            out_cluster.frames.emplace(frame.id, std::move(frame));
    }

    return true;
}

} // namespace usde
