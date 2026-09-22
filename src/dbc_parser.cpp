#include "dbc_parser.h"

#include <charconv>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <cstdlib>
#include <limits>
#include <iterator>
#include <sstream>
#include <string_view>

#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <iconv.h>
#endif
namespace usde {

// ─── helpers ────────────────────────────────────────────────────────────────

namespace {

bool IsValidUtf8(std::string_view text) {
    size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead <= 0x7fU) {
            ++i;
            continue;
        }

        uint32_t code_point = 0;
        size_t continuation_count = 0;
        if (lead >= 0xc2U && lead <= 0xdfU) {
            code_point = lead & 0x1fU;
            continuation_count = 1;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            code_point = lead & 0x0fU;
            continuation_count = 2;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            code_point = lead & 0x07U;
            continuation_count = 3;
        } else {
            return false;
        }
        if (i + continuation_count >= text.size()) return false;

        for (size_t j = 1; j <= continuation_count; ++j) {
            const auto value = static_cast<unsigned char>(text[i + j]);
            if ((value & 0xc0U) != 0x80U) return false;
            code_point = (code_point << 6U) | (value & 0x3fU);
        }

        if ((continuation_count == 2 &&
             ((lead == 0xe0U && static_cast<unsigned char>(text[i + 1]) < 0xa0U) ||
              (lead == 0xedU && static_cast<unsigned char>(text[i + 1]) >= 0xa0U))) ||
            (continuation_count == 3 &&
             ((lead == 0xf0U && static_cast<unsigned char>(text[i + 1]) < 0x90U) ||
              (lead == 0xf4U && static_cast<unsigned char>(text[i + 1]) >= 0x90U))) ||
            code_point > 0x10ffffU ||
            (code_point >= 0xd800U && code_point <= 0xdfffU) ||
            code_point == 0xfffdU) {
            return false;
        }
        i += continuation_count + 1;
    }
    return true;
}

bool ConvertGb18030ToUtf8(std::string_view source, std::string& output) {
    output.clear();
    if (source.empty()) return true;
    if (source.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return false;

#ifdef _WIN32
    constexpr unsigned int kGb18030CodePage = 54936U;
    const int source_size = static_cast<int>(source.size());
    const int wide_size = MultiByteToWideChar(
        kGb18030CodePage, MB_ERR_INVALID_CHARS, source.data(), source_size, nullptr, 0);
    if (wide_size <= 0) return false;

    std::wstring wide(static_cast<size_t>(wide_size), L'\0');
    if (MultiByteToWideChar(kGb18030CodePage, MB_ERR_INVALID_CHARS, source.data(),
                            source_size, wide.data(), wide_size) != wide_size) {
        return false;
    }

    const int utf8_size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wide_size, nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0) return false;
    output.resize(static_cast<size_t>(utf8_size));
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wide_size,
                            output.data(), utf8_size, nullptr, nullptr) != utf8_size) {
        output.clear();
        return false;
    }
#else
    iconv_t converter = iconv_open("UTF-8", "GB18030");
    if (converter == reinterpret_cast<iconv_t>(-1)) return false;

    std::vector<char> converted(std::max<size_t>(source.size() * 2U + 16U, 64U));
    char* input = const_cast<char*>(source.data());
    size_t input_left = source.size();
    char* output_ptr = converted.data();
    size_t output_left = converted.size();

    while (input_left > 0U) {
        if (iconv(converter, &input, &input_left, &output_ptr, &output_left) !=
            static_cast<size_t>(-1)) {
            break;
        }
        if (errno != E2BIG) {
            iconv_close(converter);
            return false;
        }
        const size_t used = static_cast<size_t>(output_ptr - converted.data());
        converted.resize(converted.size() * 2U);
        output_ptr = converted.data() + used;
        output_left = converted.size() - used;
    }
    iconv_close(converter);
    output.assign(converted.data(), static_cast<size_t>(output_ptr - converted.data()));
#endif

    return IsValidUtf8(output);
}

bool ReadDbcText(const std::filesystem::path& file_path, std::string& output) {
    std::ifstream input(file_path, std::ios::binary);
    if (!input.is_open()) return false;

    std::string bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    std::string_view payload(bytes);
    if (payload.size() >= 3U &&
        static_cast<unsigned char>(payload[0]) == 0xefU &&
        static_cast<unsigned char>(payload[1]) == 0xbbU &&
        static_cast<unsigned char>(payload[2]) == 0xbfU) {
        payload.remove_prefix(3);
        output.assign(payload);
        if (!IsValidUtf8(payload)) {
            std::fprintf(stderr,
                         "[USDE][DBC] invalid UTF-8 text after BOM will be omitted\n");
        }
        return true;
    } else if (IsValidUtf8(payload)) {
        output.assign(payload);
        return true;
    }

    if (ConvertGb18030ToUtf8(payload, output)) return true;

    // Preserve ASCII grammar so numeric decoding can continue. Individual
    // non-UTF-8 labels/units are omitted before metadata leaves USDE.
    output.assign(payload);
    std::fprintf(stderr,
                 "[USDE][DBC] input encoding is neither valid UTF-8 nor GB18030; "
                 "invalid metadata text will be omitted\n");
    return true;
}

inline std::string_view Trim(std::string_view s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) return {};
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline bool HasPrefix(std::string_view line, const char* prefix) {
    return line.compare(0, std::strlen(prefix), prefix) == 0;
}

// Find first non-whitespace span starting at |start|, return its [begin, end).
// Returns {npos, npos} when nothing remains.
inline std::pair<size_t, size_t>
NextToken(std::string_view s, size_t start = 0) {
    auto a = s.find_first_not_of(" \t", start);
    if (a == std::string_view::npos) return {std::string_view::npos, 0};
    auto b = s.find_first_of(" \t", a);
    if (b == std::string_view::npos) b = s.size();
    return {a, b};
}

// Parse a double from a string_view; malformed DBC numeric syntax fails loading.
inline double ParseDouble(std::string_view sv) {
    return std::stod(std::string(sv));
}

// Parse unsigned integer from a string_view.
inline uint32_t ParseUint(std::string_view sv) {
    uint32_t v = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
    if (ec != std::errc{}) v = static_cast<uint32_t>(std::stoul(std::string(sv)));
    return v;
}

bool ParseUintStrict(std::string_view text, uint32_t& value) {
    text = Trim(text);
    if (text.empty()) return false;
    const auto [ptr, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && ptr == text.data() + text.size();
}

bool CanonicalizeInteger(std::string_view text, std::string& canonical) {
    text = Trim(text);
    if (text.empty()) return false;

    bool negative = false;
    size_t index = 0;
    if (text[index] == '+' || text[index] == '-') {
        negative = text[index] == '-';
        ++index;
    }
    if (index == text.size()) return false;
    for (size_t i = index; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
    }

    while (index < text.size() && text[index] == '0') ++index;
    if (index == text.size()) {
        canonical = "0";
        return true;
    }

    canonical.assign(text.substr(index));
    if (negative) canonical.insert(canonical.begin(), '-');
    return true;
}

bool CanonicalIntegerLess(const std::string& left, const std::string& right) {
    const bool left_negative = !left.empty() && left.front() == '-';
    const bool right_negative = !right.empty() && right.front() == '-';
    if (left_negative != right_negative) return left_negative;

    const std::string_view left_digits =
        left_negative ? std::string_view(left).substr(1) : std::string_view(left);
    const std::string_view right_digits =
        right_negative ? std::string_view(right).substr(1) : std::string_view(right);
    if (left_digits.size() != right_digits.size()) {
        return left_negative
            ? left_digits.size() > right_digits.size()
            : left_digits.size() < right_digits.size();
    }
    return left_negative ? left_digits > right_digits : left_digits < right_digits;
}

bool HasFinitePhysicalValues(
    const Signal& signal,
    const std::vector<ValueState>& states) {
    for (const auto& state : states) {
        errno = 0;
        char* end = nullptr;
        const long double raw = std::strtold(state.raw_value_text.c_str(), &end);
        if (!end || *end != '\0' || errno == ERANGE || !std::isfinite(raw)) {
            return false;
        }
        const long double physical =
            raw * static_cast<long double>(signal.factor) +
            static_cast<long double>(signal.offset);
        const long double double_max =
            static_cast<long double>(std::numeric_limits<double>::max());
        if (!std::isfinite(physical) || physical > double_max || physical < -double_max) {
            return false;
        }
    }
    return true;
}

void UpsertValueState(std::vector<ValueState>& states, ValueState state) {
    const auto existing = std::find_if(
        states.begin(), states.end(), [&](const ValueState& value) {
            return value.raw_value_text == state.raw_value_text;
        });
    if (existing == states.end()) {
        states.push_back(std::move(state));
    } else {
        *existing = std::move(state);
    }
}

// Parse a BO_ line:  BO_ <ID> <Name> : <DLC> <Transmitter>
//   Example:  BO_ 256 EngineStatus: 8 ECU1
inline bool ParseBoLine(std::string_view line, Frame& frame) {
    // Skip "BO_ "
    line.remove_prefix(3);
    auto [a1, b1] = NextToken(line);
    if (a1 == std::string_view::npos) return false;
    frame.id = ParseUint(line.substr(a1, b1 - a1));

    auto [a2, b2] = NextToken(line, b1);
    if (a2 == std::string_view::npos) return false;
    // Name – may or may not end with ':'
    std::string_view name = line.substr(a2, b2 - a2);
    if (!name.empty() && name.back() == ':') name.remove_suffix(1);
    frame.name = IsValidUtf8(name) ? std::string(name) : std::string{};

    // Skip optional standalone ':' token
    size_t next_start = b2;
    auto [a3, b3] = NextToken(line, next_start);
    if (a3 == std::string_view::npos) return false;
    if (line.substr(a3, b3 - a3) == ":") {
        next_start = b3;
        a3 = std::string_view::npos;
        auto t = NextToken(line, next_start);
        a3 = t.first; b3 = t.second;
        if (a3 == std::string_view::npos) return false;
    }
    frame.dlc = ParseUint(line.substr(a3, b3 - a3));
    auto [a4, b4] = NextToken(line, b3);
    frame.transmitter.clear();
    if (a4 != std::string_view::npos) {
        const auto transmitter = line.substr(a4, b4 - a4);
        if (IsValidUtf8(transmitter)) {
            frame.transmitter.assign(transmitter);
        }
    }

    frame.signals.clear();
    return true;
}

bool ParseBuLine(std::string_view line, std::vector<std::string>& nodes) {
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) return false;

    size_t start = colon + 1U;
    while (true) {
        const auto [begin, end] = NextToken(line, start);
        if (begin == std::string_view::npos) break;
        const auto node = line.substr(begin, end - begin);
        if (IsValidUtf8(node) &&
            std::find(nodes.begin(), nodes.end(), node) == nodes.end()) {
            nodes.emplace_back(node);
        }
        start = end;
    }
    return true;
}

// Parse an SG_ line manually (no regex – avoids MSVC @ metacharacter issues).
//
// Format:
//   SG_ <Name> : <StartBit>|<Length>@<ByteOrder><Sign>(<Factor>,<Offset>)
//              [<Min>|<Max>] "<Unit>" <Receiver>
// or with multiplexer:
//   SG_ <Name> M : ...
//   SG_ <Name> m<number> : ...
//
// Returns false on syntax error.
inline bool ParseSgLine(std::string_view line, Signal& sig) {
    // Skip "SG_ "
    line.remove_prefix(3);

    // Skip leading whitespace after SG_
    auto first_non_ws = line.find_first_not_of(" \t");
    if (first_non_ws == std::string_view::npos) return false;
    line.remove_prefix(first_non_ws);

    // ── 1. Signal name (up to first whitespace or ':') ─────────────────────
    auto name_end = line.find_first_of(" \t:");
    if (name_end == std::string_view::npos) return false;
    const auto signal_name = line.substr(0, name_end);
    sig.name = IsValidUtf8(signal_name) ? std::string(signal_name) : std::string{};
    line.remove_prefix(name_end);

    // ── 2. Optional MUX token followed by the ':' separator ────────────────
    auto colon = line.find(':');
    if (colon == std::string_view::npos) return false;
    const std::string_view mux_token = Trim(line.substr(0, colon));
    if (mux_token == "M") {
        sig.is_mux_decoder = true;
    } else if (!mux_token.empty() && mux_token.front() == 'm') {
        uint32_t mux_value = 0;
        if (!ParseUintStrict(mux_token.substr(1), mux_value)) return false;
        sig.is_multiplexed = true;
        sig.mux_value = mux_value;
    } else if (!mux_token.empty()) {
        return false;
    }
    line.remove_prefix(colon + 1);

    // ── 4. StartBit | Length @ ByteOrder Sign ─────────────────────────────
    //    Example:  "0|16@1+"
    auto pipe = line.find('|');
    if (pipe == std::string_view::npos) return false;
    sig.start_bit = ParseUint(Trim(line.substr(0, pipe)));
    line.remove_prefix(pipe + 1);

    auto at_sign = line.find('@');
    if (at_sign == std::string_view::npos) return false;
    sig.bit_length = ParseUint(Trim(line.substr(0, at_sign)));
    line.remove_prefix(at_sign + 1);

    // Byte order: first char is '0' (Motorola) or '1' (Intel)
    if (line.empty()) return false;
    sig.byte_order = (line[0] == '1') ? ByteOrder::INTEL : ByteOrder::MOTOROLA;
    line.remove_prefix(1); // skip the digit
    // Next char is '+' or '-' (sign) – capture it
    if (!line.empty() && line[0] == '-') {
        sig.is_signed = true;
        line.remove_prefix(1);
    } else if (!line.empty() && line[0] == '+') {
        sig.is_signed = false;
        line.remove_prefix(1);
    }

    // ── 5. (Factor, Offset) ───────────────────────────────────────────────
    auto lp = line.find('(');
    if (lp == std::string_view::npos) return false;
    line.remove_prefix(lp + 1);

    auto comma = line.find(',');
    if (comma == std::string_view::npos) return false;
    sig.factor = ParseDouble(Trim(line.substr(0, comma)));
    line.remove_prefix(comma + 1);

    auto rp = line.find(')');
    if (rp == std::string_view::npos) return false;
    sig.offset = ParseDouble(Trim(line.substr(0, rp)));
    line.remove_prefix(rp + 1);

    // ── 6. [Min|Max] ──────────────────────────────────────────────────────
    auto lb = line.find('[');
    if (lb == std::string_view::npos) return false;
    line.remove_prefix(lb + 1);

    auto pipe2 = line.find('|');
    if (pipe2 == std::string_view::npos) return false;
    sig.min_value = ParseDouble(Trim(line.substr(0, pipe2)));
    line.remove_prefix(pipe2 + 1);

    auto rb = line.find(']');
    if (rb == std::string_view::npos) return false;
    sig.max_value = ParseDouble(Trim(line.substr(0, rb)));
    line.remove_prefix(rb + 1);

    // ── 7. "Unit" ─────────────────────────────────────────────────────────
    auto q1 = line.find('"');
    if (q1 == std::string_view::npos) return false;
    line.remove_prefix(q1 + 1);
    auto q2 = line.find('"');
    if (q2 == std::string_view::npos) return false;
    const auto unit = line.substr(0, q2);
    sig.unit = IsValidUtf8(unit) ? std::string(unit) : std::string{};

    return true;
}

bool ParseSignalCommentLine(
    std::string_view line,
    uint32_t& message_id,
    std::string& signal_name,
    std::string& description) {
    line.remove_prefix(7); // CM_ SG_
    const auto [message_begin, message_end] = NextToken(line);
    if (message_begin == std::string_view::npos ||
        !ParseUintStrict(line.substr(message_begin, message_end - message_begin), message_id)) {
        return false;
    }
    const auto [signal_begin, signal_end] = NextToken(line, message_end);
    if (signal_begin == std::string_view::npos) return false;
    const auto signal = line.substr(signal_begin, signal_end - signal_begin);
    if (!IsValidUtf8(signal)) return false;
    signal_name.assign(signal);

    size_t cursor = line.find('"', signal_end);
    if (cursor == std::string_view::npos) return false;
    ++cursor;
    description.clear();
    bool closed = false;
    while (cursor < line.size()) {
        const char value = line[cursor++];
        if (value == '"') {
            closed = true;
            break;
        }
        if (value == '\\' && cursor < line.size()) {
            const char escaped = line[cursor++];
            switch (escaped) {
                case 'n': description.push_back('\n'); break;
                case 'r': description.push_back('\r'); break;
                case 't': description.push_back('\t'); break;
                case '"': description.push_back('"'); break;
                case '\\': description.push_back('\\'); break;
                default:
                    description.push_back('\\');
                    description.push_back(escaped);
                    break;
            }
        } else {
            description.push_back(value);
        }
    }
    if (!closed || !IsValidUtf8(description)
        || description.find_first_of("\r\n") != std::string::npos) {
        return false;
    }
    return true;
}

bool ParseValLine(
    std::string_view line,
    uint32_t& message_id,
    std::string& signal_name,
    std::vector<ValueState>& states) {
    line.remove_prefix(4); // VAL_
    states.clear();
    const auto [message_begin, message_end] = NextToken(line);
    if (message_begin == std::string_view::npos ||
        !ParseUintStrict(line.substr(message_begin, message_end - message_begin), message_id)) {
        return false;
    }

    const auto [signal_begin, signal_end] = NextToken(line, message_end);
    if (signal_begin == std::string_view::npos) return false;
    const auto signal = line.substr(signal_begin, signal_end - signal_begin);
    if (!IsValidUtf8(signal)) return false;
    signal_name.assign(signal);

    std::vector<ValueState> parsed_states;
    size_t cursor = signal_end;
    while (true) {
        cursor = line.find_first_not_of(" \t\r\n", cursor);
        if (cursor == std::string_view::npos || line[cursor] == ';') break;

        const size_t raw_begin = cursor;
        cursor = line.find_first_of(" \t\r\n", cursor);
        if (cursor == std::string_view::npos) return false;

        std::string raw_value;
        if (!CanonicalizeInteger(line.substr(raw_begin, cursor - raw_begin), raw_value)) {
            return false;
        }

        cursor = line.find_first_not_of(" \t\r\n", cursor);
        if (cursor == std::string_view::npos || line[cursor] != '"') return false;
        ++cursor;

        std::string label;
        bool closed = false;
        while (cursor < line.size()) {
            const char value = line[cursor++];
            if (value == '"') {
                closed = true;
                break;
            }
            if (value == '\\' && cursor < line.size()) {
                const char escaped = line[cursor++];
                switch (escaped) {
                    case 'n': label.push_back('\n'); break;
                    case 'r': label.push_back('\r'); break;
                    case 't': label.push_back('\t'); break;
                    default:
                        label.push_back('\\');
                        label.push_back(escaped);
                        break;
                }
            } else {
                label.push_back(value);
            }
        }
        if (!closed) return false;

        if (Trim(label).empty() || !IsValidUtf8(label) ||
            label.find_first_of("\r\n") != std::string::npos) {
            std::fprintf(
                stderr,
                "[USDE][DBC] rejected empty or invalid UTF-8 state label: "
                "message=%u signal=%s\n",
                static_cast<unsigned int>(message_id),
                signal_name.c_str());
            return false;
        }
        UpsertValueState(parsed_states, ValueState{std::move(raw_value), std::move(label)});
    }

    std::sort(parsed_states.begin(), parsed_states.end(), [](const ValueState& left, const ValueState& right) {
        return CanonicalIntegerLess(left.raw_value_text, right.raw_value_text);
    });
    states = std::move(parsed_states);
    return true;
}

} // anonymous namespace

// ─── public API ─────────────────────────────────────────────────────────────

bool LoadDBC(const std::filesystem::path& file_path, NetworkCluster& out_cluster) {
    std::string dbc_text;
    if (!ReadDbcText(file_path, dbc_text)) return false;
    std::istringstream stream(dbc_text);

    Frame  current_frame{};
    bool   has_frame = false;
    size_t line_no   = 0;
    std::string line;
    bool   has_signal_definition = false;
    std::unordered_map<uint32_t,
        std::unordered_map<std::string, std::vector<ValueState>>> pending_states;
    std::unordered_map<uint32_t,
        std::unordered_map<std::string, std::string>> pending_descriptions;


    auto save_frame = [&]() {
        if (has_frame) {
            out_cluster.frames.emplace(current_frame.id, std::move(current_frame));
            current_frame = Frame{};
            has_frame = false;
        }
    };

    while (std::getline(stream, line)) {
        ++line_no;
        std::string_view sv = Trim(line);
        if (sv.empty()) continue;

        // ── BU_ (declared network nodes) ─────────────────────────────────
        if (HasPrefix(sv, "BU_:") || HasPrefix(sv, "BU_ :")) {
            if (!ParseBuLine(sv, out_cluster.nodes)) {
                std::fprintf(stderr, "[USDE][DBC] ignored malformed BU_ at line %zu\n", line_no);
            }
            continue;
        }

        // ── BO_ (message definition) ─────────────────────────────────────
        if (HasPrefix(sv, "BO_ ")) {
            save_frame();
            if (!ParseBoLine(sv, current_frame)) return false;
            has_frame = true;
            continue;
        }

        // ── SG_ (signal definition) ──────────────────────────────────────
        if (HasPrefix(sv, "SG_ ") && has_frame) {
            Signal sig;
            if (!ParseSgLine(sv, sig)) return false;
            current_frame.signals.push_back(std::move(sig));
            has_signal_definition = true;
            continue;
        }
        // ── VAL_ (raw integer to state label) ─────────────────────────────
        if (HasPrefix(sv, "VAL_ ")) {
            uint32_t message_id = 0;
            std::string signal_name;
            std::vector<ValueState> states;
            if (ParseValLine(sv, message_id, signal_name, states)) {
                pending_states[message_id][std::move(signal_name)] = std::move(states);
            } else {
                // A malformed state table does not block numeric DBC loading.
                if (!signal_name.empty()) {
                    pending_states[message_id][signal_name].clear();
                }
                std::fprintf(
                    stderr,
                    "[USDE][DBC] ignored malformed VAL_: line=%zu message=%u signal=%s\n",
                    line_no,
                    static_cast<unsigned int>(message_id),
                    signal_name.c_str());
            }
            continue;
        }

        // ── CM_ SG_ (signal description) ────────────────────────────────
        if (HasPrefix(sv, "CM_ SG_ ")) {
            uint32_t message_id = 0;
            std::string signal_name;
            std::string description;
            if (ParseSignalCommentLine(sv, message_id, signal_name, description)) {
                pending_descriptions[message_id][std::move(signal_name)] =
                    std::move(description);
            } else {
                std::fprintf(stderr, "[USDE][DBC] ignored malformed CM_ SG_: line=%zu\n", line_no);
            }
        }
    }

    save_frame();
    for (auto& [message_id, signals] : pending_states) {
        const auto frame = out_cluster.frames.find(message_id);
        if (frame == out_cluster.frames.end()) continue;
        for (auto& signal : frame->second.signals) {
            const auto states = signals.find(signal.name);
            if (states != signals.end() &&
                HasFinitePhysicalValues(signal, states->second)) {
                signal.value_states = std::move(states->second);
                for (auto& state : signal.value_states) {
                    const std::string prefix = signal.name + "_" + state.raw_value_text + "_";
                    if (state.label.compare(0, prefix.size(), prefix) == 0) {
                        state.label.erase(0, prefix.size());
                    }
                }
            } else if (states != signals.end()) {
                std::fprintf(
                    stderr,
                    "[USDE][DBC] ignored non-finite VAL_: message=%u signal=%s\n",
                    static_cast<unsigned int>(message_id),
                    signal.name.c_str());
            }
        }
    }

    for (auto& [message_id, signals] : pending_descriptions) {
        const auto frame = out_cluster.frames.find(message_id);
        if (frame == out_cluster.frames.end()) continue;
        for (auto& signal : frame->second.signals) {
            const auto description = signals.find(signal.name);
            if (description != signals.end()) signal.description = std::move(description->second);
        }
    }

    return has_signal_definition;
}

} // namespace usde
