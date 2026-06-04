#include "ldf_parser.h"

#include <charconv>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace usde {

namespace {

inline std::string_view Trim(std::string_view s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) return {};
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Strip // and /* */ comments in-place.
static std::string StripComments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    bool in_block = false;
    for (size_t i = 0; i < src.size(); ++i) {
        if (in_block) {
            if (i + 1 < src.size() && src[i] == '*' && src[i + 1] == '/') {
                in_block = false;
                ++i;
            }
            continue;
        }
        if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '*') {
            in_block = true;
            ++i;
            continue;
        }
        if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '/') {
            // Skip to end of line
            auto nl = src.find('\n', i);
            if (nl == std::string::npos) break;
            i = nl;
            out += '\n';
            continue;
        }
        out += src[i];
    }
    return out;
}

// Split by delimiter, trim each token.
static std::vector<std::string_view> SplitTrimmed(std::string_view sv, char delim) {
    std::vector<std::string_view> tokens;
    while (!sv.empty()) {
        auto pos = sv.find(delim);
        if (pos == std::string_view::npos) {
            auto t = Trim(sv);
            if (!t.empty()) tokens.push_back(t);
            break;
        }
        auto t = Trim(sv.substr(0, pos));
        if (!t.empty()) tokens.push_back(t);
        sv.remove_prefix(pos + 1);
    }
    return tokens;
}

// Find a top-level block by keyword and extract lines between { and }.
static std::vector<std::string_view>
ExtractBlock(const std::vector<std::string_view>& lines,
             std::string_view keyword) {
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string_view l = lines[i];
        auto kw_pos = l.find(keyword);
        if (kw_pos == std::string_view::npos) continue;
        // Check that keyword is at start (possibly after whitespace)
        auto before = Trim(l.substr(0, kw_pos));
        if (!before.empty()) continue;
        // Find opening brace (may be on same or next line)
        size_t brace_line = i;
        while (brace_line < lines.size() &&
               lines[brace_line].find('{') == std::string_view::npos)
            ++brace_line;
        if (brace_line >= lines.size()) return {};

        std::vector<std::string_view> block;
        size_t idx = brace_line + 1;
        int depth = 1;
        while (idx < lines.size() && depth > 0) {
            std::string_view bl = lines[idx];
            for (char c : bl) {
                if (c == '{') ++depth;
                else if (c == '}') --depth;
            }
            if (depth > 0) block.push_back(bl);
            ++idx;
        }
        return block;
    }
    return {};
}

} // anonymous namespace

bool LoadLDF(const std::filesystem::path& file_path, NetworkCluster& out_cluster) {
    // 1. Read file and strip comments
    std::ifstream ifs(file_path, std::ios::in);
    if (!ifs.is_open()) return false;
    std::string raw((std::istreambuf_iterator<char>(ifs)),
                     std::istreambuf_iterator<char>());
    ifs.close();

    std::string content = StripComments(raw);

    // Split into lines
    std::vector<std::string_view> lines;
    {
        std::string_view sv(content);
        size_t start = 0;
        while (start < sv.size()) {
            auto end = sv.find('\n', start);
            if (end == std::string_view::npos) end = sv.size();
            auto line = Trim(sv.substr(start, end - start));
            if (!line.empty()) lines.push_back(line);
            start = end + 1;
        }
    }

    // 2. Parse Signals { ... }
    //    Format: SignalName: Size, InitValue, Publisher, Subscriber1, ... ;
    struct RawSignal {
        uint32_t size   = 0;
        double   offset = 0.0;
        double   factor = 1.0;
        double   min_val = 0.0;
        double   max_val = 0.0;
        std::string unit;
    };
    std::unordered_map<std::string, RawSignal> sig_map;

    {
        auto block = ExtractBlock(lines, "Signals");
        for (auto& bl : block) {
            std::string_view l = Trim(bl);
            if (l.empty()) continue;

            auto colon = l.find(':');
            if (colon == std::string_view::npos) continue;

            std::string name(l.substr(0, colon));
            auto rest = l.substr(colon + 1);

            // Remove trailing ';'
            auto semi = rest.find(';');
            if (semi != std::string_view::npos) rest = rest.substr(0, semi);

            auto tokens = SplitTrimmed(rest, ',');
            if (tokens.size() < 2) continue;

            RawSignal rs;
            rs.size = static_cast<uint32_t>(std::stoul(std::string(tokens[0])));
            if (tokens.size() >= 2)
                rs.offset = std::stod(std::string(tokens[1]));

            sig_map[name] = rs;
        }
    }

    // 3. Parse Signal_encoding_types { ... } (optional - factors/units)
    {
        auto block = ExtractBlock(lines, "Signal_encoding_types");
        std::string current_enc;
        for (auto& bl : block) {
            std::string_view l = Trim(bl);
            if (l.empty()) continue;

            // Encoding type header: EncName {
            if (l.find('{') != std::string_view::npos &&
                l.find('=') == std::string_view::npos &&
                l.find("logical_value") == std::string_view::npos &&
                l.find("physical_value") == std::string_view::npos) {
                auto brace = l.find('{');
                current_enc = std::string(Trim(l.substr(0, brace)));
                continue;
            }

            // physical_value, factor, offset, min, max, "unit" ;
            if (l.find("physical_value") != std::string_view::npos) {
                auto eq = l.find('=');
                if (eq == std::string_view::npos) continue;
                auto semi = l.find(';', eq);
                std::string_view val = l.substr(eq + 1,
                    (semi == std::string_view::npos ? l.size() : semi) - eq - 1);
                auto toks = SplitTrimmed(val, ',');
                // Find signals that use this encoding via Signal_representation
                // For now, just store the encoding data for later matching
                (void)toks; // Will be used below
            }
        }
    }

    // 3b. Parse Signal_representation { ... } to map encodings to signals
    //     Format: EncodingName: Signal1, Signal2, ... ;
    // Then use encoding data to fill factor/offset/unit.
    {
        auto enc_block = ExtractBlock(lines, "Signal_encoding_types");
        std::unordered_map<std::string, std::vector<std::string>> enc_data;
        std::string current_enc;
        for (auto& bl : enc_block) {
            std::string_view l = Trim(bl);
            if (l.empty()) continue;
            if (l.find('{') != std::string_view::npos &&
                l.find('=') == std::string_view::npos &&
                l.find("logical_value") == std::string_view::npos &&
                l.find("physical_value") == std::string_view::npos) {
                auto brace = l.find('{');
                current_enc = std::string(Trim(l.substr(0, brace)));
                continue;
            }
            if (l.find("physical_value") != std::string_view::npos) {
                auto eq = l.find('=');
                if (eq == std::string_view::npos) continue;
                auto semi = l.find(';', eq);
                std::string_view val = l.substr(eq + 1,
                    (semi == std::string_view::npos ? l.size() : semi) - eq - 1);
                auto toks = SplitTrimmed(val, ',');
                if (toks.size() >= 4) {
                    enc_data[current_enc] = {
                        std::string(toks[0]), // factor
                        std::string(toks[1]), // offset
                        std::string(toks[2]), // min
                        std::string(toks[3]), // max
                        toks.size() >= 5 ? std::string(toks[4]) : ""
                    };
                }
            }
        }

        auto repr_block = ExtractBlock(lines, "Signal_representation");
        for (auto& bl : repr_block) {
            std::string_view l = Trim(bl);
            if (l.empty()) continue;
            auto colon = l.find(':');
            if (colon == std::string_view::npos) continue;
            auto semi = l.find(';');
            std::string enc_name(l.substr(0, colon));
            auto rest = l.substr(colon + 1,
                (semi == std::string_view::npos ? l.size() : semi) - colon - 1);
            auto sig_names = SplitTrimmed(rest, ',');

            auto it = enc_data.find(enc_name);
            if (it == enc_data.end()) continue;

            for (auto& sn : sig_names) {
                auto si = sig_map.find(std::string(sn));
                if (si != sig_map.end()) {
                    si->second.factor = std::stod(it->second[0]);
                    si->second.offset = std::stod(it->second[1]);
                    si->second.min_val = std::stod(it->second[2]);
                    si->second.max_val = std::stod(it->second[3]);
                    if (it->second.size() > 4) {
                        std::string u = it->second[4];
                        if (!u.empty() && u.front() == '"') u.erase(0, 1);
                        if (!u.empty() && u.back() == '"') u.pop_back();
                        si->second.unit = u;
                    }
                }
            }
        }
    }

    // 4. Parse Frames { ... }
    //    Format: FrameName: ID, Publisher, Size {
    //      SignalName, ByteOffset ;
    //      ...
    //    }
    {
        auto block = ExtractBlock(lines, "Frames");
        Frame current_frame{};
        bool  has_frame = false;

        auto save_frame = [&]() {
            if (has_frame) {
                out_cluster.frames.emplace(current_frame.id,
                                           std::move(current_frame));
                current_frame = Frame{};
                has_frame = false;
            }
        };

        for (size_t i = 0; i < block.size(); ++i) {
            std::string_view l = Trim(block[i]);
            if (l.empty()) continue;

            // Frame header: Name: ID, Publisher, Size {
            // ID may be hex (0x3C) or decimal (3)
            if (l.find('{') != std::string_view::npos &&
                l.find(':') != std::string_view::npos &&
                l.find("logical_value") == std::string_view::npos &&
                l.find("physical_value") == std::string_view::npos) {
                save_frame();

                auto colon = l.find(':');
                if (colon == std::string_view::npos) continue;
                current_frame.name = std::string(Trim(l.substr(0, colon)));

                auto rest = l.substr(colon + 1);
                auto brace = rest.find('{');
                if (brace != std::string_view::npos) rest = rest.substr(0, brace);

                auto tokens = SplitTrimmed(rest, ',');
                if (tokens.size() >= 3) {
                    // Parse ID (hex or decimal)
                    std::string_view id_sv = tokens[0];
                    if (id_sv.size() > 2 &&
                        (id_sv[1] == 'x' || id_sv[1] == 'X')) {
                        id_sv.remove_prefix(2);
                        uint64_t pid = 0;
                        std::from_chars(id_sv.data(),
                                        id_sv.data() + id_sv.size(), pid, 16);
                        current_frame.id = static_cast<uint32_t>(pid);
                    } else {
                        current_frame.id = static_cast<uint32_t>(
                            std::stoul(std::string(id_sv)));
                    }
                    current_frame.dlc = static_cast<uint32_t>(
                        std::stoul(std::string(tokens[2])));
                    current_frame.signals.clear();
                    has_frame = true;
                }
                continue;
            }

            // Close brace at block level signals end of frame
            if (l.find('}') != std::string_view::npos &&
                l.find(',') == std::string_view::npos &&
                l.find(';') == std::string_view::npos) {
                save_frame();
                continue;
            }

            // Signal reference: SignalName, ByteOffset ;
            if (has_frame && l.find(',') != std::string_view::npos &&
                l.find(';') != std::string_view::npos) {
                auto semi = l.find(';');
                std::string_view entry = l.substr(0, semi);
                auto tokens = SplitTrimmed(entry, ',');
                if (tokens.size() >= 2) {
                    auto it = sig_map.find(std::string(tokens[0]));
                    if (it != sig_map.end()) {
                        Signal sig;
                        sig.name       = std::string(tokens[0]);
                        sig.bit_length = it->second.size;
                        uint32_t byte_off = static_cast<uint32_t>(
                            std::stoul(std::string(tokens[1])));
                        sig.start_bit  = byte_off * 8;
                        sig.byte_order = ByteOrder::INTEL;
                        sig.factor     = it->second.factor;
                        sig.offset     = it->second.offset;
                        sig.min_value  = it->second.min_val;
                        sig.max_value  = it->second.max_val;
                        sig.unit       = it->second.unit;
                        current_frame.signals.push_back(std::move(sig));
                    }
                }
            }
        }
        save_frame();
    }

    return true;
}

} // namespace usde
