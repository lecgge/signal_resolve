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

// LDF 的信号位偏移有两种写法，必须区分（2026-10-19 现场踩坑）：
//
// A) 帧内偏移：StartByte 取值 1..DLC，位偏移 = (StartByte-1)*8
//    例：Door.ldf 的 `DWFR_WinPos, 0` → start_bit=0；`DWFR_ResponseError, 7` → 56
//
// B) 总线级全局偏移：多个帧共享一个地址空间，StartByte 可以远超本帧 DLC
//    现场 FICM_LIN1 的 Frame 0x01（DLC=8）里偏移却是 24..49 —— 因为这个
//    LDF 把整条总线的响应帧接在一个连续地址空间里编址。
//    例：`EPTRdy_NewL_l, 39` → 39 > DLC(8)，只能是 B。
//
// 原实现两种都不对：把 StartByte 当成**位偏移**直接用（字节 39 变成位 39），
// 换算后又一律乘 8（变成 312），位置完全错位；更糟的是缓冲区只按 dlc 开，
// PackBits 越界被静默丢弃，encode_frame 恒返回全 0 且不报错。
//
// 这里保留**原始字节号**并按实际最大字节号扩容，让 encode 能写对位置；
// 若目标帧需要按物理 DLC 截断，调用方应改用该帧对应 DLC 内的信号子集。
static inline uint32_t LdfStartByteToBitOffset(uint32_t start_byte) {
    return start_byte == 0 ? 0u : (start_byte - 1u) * 8u;
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

            sig_map[name] = rs;
        }
    }

    // 3. 解析信号编码，再通过 Signal_representation 关联到信号。
    {
        auto enc_block = ExtractBlock(lines, "Signal_encoding_types");
        struct Encoding {
            double factor = 1.0;
            double offset = 0.0;
            double min_val = 0.0;
            double max_val = 0.0;
            std::string unit;
        };
        std::unordered_map<std::string, Encoding> enc_data;
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
                auto comma = l.find(',');
                if (current_enc.empty() || comma == std::string_view::npos) continue;
                auto semi = l.find(';', comma);
                std::string_view val = l.substr(comma + 1,
                    (semi == std::string_view::npos ? l.size() : semi) - comma - 1);
                auto toks = SplitTrimmed(val, ',');
                if (toks.size() < 5) continue;

                const double raw_min = std::stod(std::string(toks[0]));
                const double raw_max = std::stod(std::string(toks[1]));
                Encoding encoding;
                encoding.factor = std::stod(std::string(toks[2]));
                encoding.offset = std::stod(std::string(toks[3]));
                encoding.min_val = raw_min * encoding.factor + encoding.offset;
                encoding.max_val = raw_max * encoding.factor + encoding.offset;
                encoding.unit = std::string(toks[4]);
                if (!encoding.unit.empty() && encoding.unit.front() == '"') {
                    encoding.unit.erase(0, 1);
                }
                if (!encoding.unit.empty() && encoding.unit.back() == '"') {
                    encoding.unit.pop_back();
                }
                enc_data[current_enc] = std::move(encoding);
            }
        }

        auto repr_block = ExtractBlock(lines, "Signal_representation");
        for (auto& bl : repr_block) {
            std::string_view l = Trim(bl);
            if (l.empty()) continue;
            auto colon = l.find(':');
            if (colon == std::string_view::npos) continue;
            auto semi = l.find(';');
            std::string enc_name(Trim(l.substr(0, colon)));
            auto rest = l.substr(colon + 1,
                (semi == std::string_view::npos ? l.size() : semi) - colon - 1);
            auto sig_names = SplitTrimmed(rest, ',');

            auto it = enc_data.find(enc_name);
            if (it == enc_data.end()) continue;

            for (auto& sn : sig_names) {
                auto si = sig_map.find(std::string(sn));
                if (si != sig_map.end()) {
                    si->second.factor = it->second.factor;
                    si->second.offset = it->second.offset;
                    si->second.min_val = it->second.min_val;
                    si->second.max_val = it->second.max_val;
                    si->second.unit = it->second.unit;
                }
            }
        }
    }
    // 4. Parse Encoding_Response（**LDF 里信号位布局的权威来源**）
    //    Format: FrameName: ID, Publisher, Size {
    //      SignalName: Size, StartByte, Publisher, Subscriber1, ... ;
    //    }
    //
    //    2026-10-19 现场修正：原实现只解析 `Frames` 段，而 Mentor/Vector 生成的
    //    真实 LDF 把位布局放在 `Encoding_Response` 段 —— 两段格式不同（帧头是
    //    `Name: ID, Publisher, Size`，信号行多一个 Publisher），于是信号全解析不到，
    //    start_bit 停留在 0、factor 停留在 1.0，encode_frame 恒返回全 0。
    //    这里改成：优先读 Encoding_Response，读不到才回退 Frames（兼容
    //    某些工具导出的 Frames 段变体）。
    {
        auto block = ExtractBlock(lines, "Encoding_Response");
        if (block.empty()) {
            block = ExtractBlock(lines, "Encoding_Request");
        }
        if (block.empty()) {
            block = ExtractBlock(lines, "Frames");   // 回退
        }
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

            // Signal reference. 两种格式都要认：
            //   Encoding_Response: Name: Size, StartByte, Publisher, Subs... ;
            //   Frames (fallback) : Name, BitOffset ;
            if (has_frame && l.find(',') != std::string_view::npos &&
                l.find(';') != std::string_view::npos) {
                auto semi = l.find(';');
                std::string_view entry = l.substr(0, semi);

                // 先剥掉 Encoding_Response 的 "Name: " 前缀
                std::string_view body = Trim(entry);
                if (body.find(':') != std::string_view::npos) {
                    auto c = body.find(':');
                    body = Trim(body.substr(c + 1));
                }

                auto tokens = SplitTrimmed(body, ',');
                if (tokens.size() >= 2) {
                    auto it = sig_map.find(std::string(tokens[0]));
                    if (it != sig_map.end()) {
                        Signal sig;
                        sig.name       = std::string(tokens[0]);
                        // Encoding_Response 的第 1 个数是 Size、第 2 个是 StartByte；
                        // Frames 回退格式第 1 个数直接是 BitOffset。两种都要落到
                        // 同一个 0-based 位偏移上。
                        sig.bit_length = it->second.size;
                        uint32_t raw = static_cast<uint32_t>(
                            std::stoul(std::string(tokens[1])));
                        sig.start_bit  = LdfStartByteToBitOffset(raw);
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
