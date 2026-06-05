#include "dbc_parser.h"

#include <charconv>
#include <cstring>
#include <fstream>
#include <string_view>

namespace usde {

// ─── helpers ────────────────────────────────────────────────────────────────

namespace {

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

// Parse a double from a string_view.  Returns 0.0 on failure.
inline double ParseDouble(std::string_view sv) {
    double v = 0.0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
    if (ec != std::errc{}) v = std::stod(std::string(sv));
    return v;
}

// Parse unsigned integer from a string_view.
inline uint32_t ParseUint(std::string_view sv) {
    uint32_t v = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
    if (ec != std::errc{}) v = static_cast<uint32_t>(std::stoul(std::string(sv)));
    return v;
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
    frame.name = std::string(name);

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

    frame.signals.clear();
    return true;
}

// Parse an SG_ line manually (no regex – avoids MSVC @ metacharacter issues).
//
// Format:
//   SG_ <Name> : <StartBit>|<Length>@<ByteOrder><Sign>(<Factor>,<Offset>)
//              [<Min>|<Max>] "<Unit>" <Receiver>
// or with multiplexer:
//   SG_ M <Name> : ...
//
// Returns false on syntax error.
inline bool ParseSgLine(std::string_view line, Signal& sig) {
    // Skip "SG_ "
    line.remove_prefix(3);

    // Skip leading whitespace after SG_
    auto first_non_ws = line.find_first_not_of(" \t");
    if (first_non_ws == std::string_view::npos) return false;
    line.remove_prefix(first_non_ws);

    // ── 1. Multiplexer flag (optional single 'M' or 'm' followed by space) ──
    if (!line.empty() && (line[0] == 'M' || line[0] == 'm') &&
        line.size() > 1 && line[1] == ' ') {
        sig.is_multiplexed = true;
        sig.mux_value      = (line[0] == 'M') ? 0xFFFFFFFF : 0;
        line.remove_prefix(2);
    }

    // ── 2. Signal name (up to first whitespace or ':') ─────────────────────
    auto name_end = line.find_first_of(" \t:");
    if (name_end == std::string_view::npos) return false;
    sig.name = std::string(line.substr(0, name_end));
    line.remove_prefix(name_end);

    // ── 3. Skip whitespace and the ':' separator ──────────────────────────
    auto colon = line.find(':');
    if (colon == std::string_view::npos) return false;
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
    sig.unit = std::string(line.substr(0, q2));

    return true;
}

} // anonymous namespace

// ─── public API ─────────────────────────────────────────────────────────────

bool LoadDBC(const std::filesystem::path& file_path, NetworkCluster& out_cluster) {
    std::ifstream ifs(file_path, std::ios::in);
    if (!ifs.is_open()) return false;

    Frame  current_frame{};
    bool   has_frame = false;
    size_t line_no   = 0;
    std::string line;

    auto save_frame = [&]() {
        if (has_frame) {
            out_cluster.frames.emplace(current_frame.id, std::move(current_frame));
            current_frame = Frame{};
            has_frame = false;
        }
    };

    while (std::getline(ifs, line)) {
        ++line_no;
        std::string_view sv = Trim(line);
        if (sv.empty()) continue;

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
        }
    }

    save_frame();
    return true;
}

} // namespace usde
