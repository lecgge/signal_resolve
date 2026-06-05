#include "codec_engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace usde {

// ============================================================================
// Bit-Stream Extractor
// ============================================================================
//
// Intel (Little-Endian, LSB first):
//   Bit numbering is absolute from byte 0, bit 0 upward.
//   start_bit = position of the signal's LSB.
//   Signal occupies bits [start_bit, start_bit + bit_length - 1].
//
// Motorola (Big-Endian, MSB first):
//   Bit 0 is byte 0 bit 0; bit 7 is byte 0 bit 7; bit 8 is byte 1 bit 0...
//   start_bit = position of the signal's MSB.
//   Signal occupies bits [start_bit - bit_length + 1, start_bit].
//   Bytes increase as we go from MSB to LSB.
//
// ============================================================================

uint64_t CodecEngine::ExtractBits(
    const uint8_t* data, size_t size,
    uint32_t start_bit, uint32_t bit_length, ByteOrder byte_order)
{
    if (bit_length == 0 || bit_length > 64 || !data) return 0;

    if (byte_order == ByteOrder::INTEL) {
        // ── Intel: start_bit = LSB ──────────────────────────────────────
        // Byte index = start_bit / 8,  bit index within byte = start_bit % 8
        uint32_t first_byte = start_bit / 8;
        uint32_t msb_pos    = start_bit + bit_length - 1;
        uint32_t last_byte  = msb_pos / 8;

        if (first_byte >= size) return 0;

        // Assemble up to 8 bytes into a little-endian uint64
        uint64_t val = 0;
        uint32_t bytes_needed = last_byte - first_byte + 1;
        if (bytes_needed > 8) bytes_needed = 8;
        for (uint32_t i = 0; i < bytes_needed; ++i) {
            uint32_t bi = first_byte + i;
            if (bi < size)
                val |= static_cast<uint64_t>(data[bi]) << (i * 8);
        }

        // Shift right to align LSB, then mask
        uint32_t shift = start_bit - first_byte * 8;
        val >>= shift;

        uint64_t mask = (bit_length == 64) ? ~uint64_t(0)
                                           : ((uint64_t(1) << bit_length) - 1);
        return val & mask;

    } else {
        // ── Motorola: start_bit = MSB (DBC reversed bit numbering) ─────
        //
        // DBC: bit 0 = byte0.MSB, bit 7 = byte0.LSB, bit 8 = byte1.MSB...
        //   lsb_in_byte = 7 - (start_bit % 8)
        //   Absolute LSB position = start_bit/8 * 8 + lsb_in_byte
        //   Absolute MSB position = start_bit/8 * 8 + lsb_in_byte + bit_length - 1
        //   First byte = start_bit / 8
        //   Last byte  = (first_byte * 8 + lsb_in_byte + bit_length - 1) / 8
        //
        uint32_t first_byte   = start_bit / 8;
        uint32_t lsb_in_byte  = 7 - (start_bit % 8);
        uint32_t last_byte    = (first_byte * 8 + lsb_in_byte + bit_length - 1) / 8;

        if (last_byte >= size) return 0;

        // Assemble bytes ascending into big-endian uint64
        uint64_t val = 0;
        uint32_t bytes_needed = last_byte - first_byte + 1;
        if (bytes_needed > 8) bytes_needed = 8;
        for (uint32_t i = 0; i < bytes_needed; ++i) {
            uint32_t bi = first_byte + i;
            if (bi < size)
                val = (val << 8) | data[bi];
        }

        // Shift right to align LSB to bit 0
        // Single-byte: shift within the byte = start_bit % 8
        // Multi-byte:  shift = first_byte*8 + lsb_in_byte
        uint32_t shift = (first_byte == last_byte)
            ? (start_bit % 8)
            : (static_cast<uint32_t>(first_byte) * 8 + lsb_in_byte);
        val >>= shift;

        uint64_t mask = (bit_length == 64) ? ~uint64_t(0)
                                           : ((uint64_t(1) << bit_length) - 1);
        return val & mask;
    }
}

// ============================================================================
// Bit-Stream Packer (non-destructive to surrounding bits)
// ============================================================================

void CodecEngine::PackBits(
    uint8_t* data, size_t size,
    uint32_t start_bit, uint32_t bit_length,
    ByteOrder byte_order, uint64_t raw_value)
{
    if (bit_length == 0 || bit_length > 64 || !data) return;

    // Clamp value to bit_length bits
    uint64_t mask = (bit_length == 64) ? ~uint64_t(0)
                                       : ((uint64_t(1) << bit_length) - 1);
    raw_value &= mask;

    if (byte_order == ByteOrder::INTEL) {
        // ── Intel: start_bit = LSB ──────────────────────────────────────
        uint32_t first_byte = start_bit / 8;
        uint32_t msb_pos    = start_bit + bit_length - 1;
        uint32_t last_byte  = msb_pos / 8;
        uint32_t bit_offset = start_bit - first_byte * 8;

        uint64_t val_shifted = raw_value << bit_offset;

        for (uint32_t bi = first_byte; bi <= last_byte && bi < size; ++bi) {
            // Which bits of val_shifted fall into this byte?
            uint32_t byte_idx = bi - first_byte;
            uint8_t  new_byte = static_cast<uint8_t>(val_shifted >> (byte_idx * 8));

            // Build mask for the bits this signal occupies in this byte
            uint8_t sig_mask = 0xFF;
            if (bi == first_byte && bit_offset > 0)
                sig_mask &= static_cast<uint8_t>(0xFF << bit_offset);
            if (bi == last_byte) {
                uint32_t top_bits = (start_bit + bit_length) - last_byte * 8;
                if (top_bits < 8)
                    sig_mask &= static_cast<uint8_t>((1u << top_bits) - 1);
            }

            data[bi] = (data[bi] & ~sig_mask) | (new_byte & sig_mask);
        }

    } else {
        // ── Motorola: start_bit = MSB (DBC reversed bit numbering) ─────
        //   DBC: bit 0 = byte0.MSB, bit 7 = byte0.LSB, bit 8 = byte1.MSB...
        //   lsb_in_byte = 7 - (start_bit % 8)
        //   first_byte  = start_bit / 8
        //   last_byte   = (start_bit + bit_length - 1) / 8
        //   msb_in_byte = (start_bit + bit_length - 1) % 8
        //
        uint32_t first_byte   = start_bit / 8;
        uint32_t lsb_in_byte  = 7 - (start_bit % 8);
        uint32_t last_byte    = (first_byte * 8 + lsb_in_byte + bit_length - 1) / 8;
        int32_t  msb_in_byte  = static_cast<int32_t>(start_bit + bit_length - 1) % 8;

        if (last_byte >= size) return;

        // Single-byte: place value at bit (start_bit % 8) to match ExtractBits
        if (first_byte == last_byte) {
            uint32_t bit_pos = start_bit % 8;
            uint64_t m = (bit_length == 64) ? ~0ULL : ((1ULL << bit_length) - 1);
            uint8_t  mask = static_cast<uint8_t>(m << bit_pos);
            uint8_t  val  = static_cast<uint8_t>((raw_value & m) << bit_pos);
            data[first_byte] = (data[first_byte] & ~mask) | val;
            return;
        }

        uint32_t shift = static_cast<uint32_t>(first_byte) * 8 + lsb_in_byte;

        for (uint32_t bi = last_byte; bi >= first_byte && bi < size; --bi) {
            // This byte covers bits [(last_byte-bi)*8  .. (last_byte-bi)*8+7]
            // in the assembled big-endian word. The signal starts at `shift`
            // from the LSB of the assembled word.
            // So byte bi receives signal bits:
            //   sig_bit_start = (last_byte - bi) * 8 - shift
            uint32_t byte_idx = last_byte - bi;
            int32_t sig_bit_start = static_cast<int32_t>(byte_idx * 8) -
                                     static_cast<int32_t>(shift);
            uint8_t new_byte = 0;
            if (sig_bit_start < static_cast<int32_t>(bit_length) && sig_bit_start > -8) {
                uint32_t rshift = (sig_bit_start >= 0)
                                      ? static_cast<uint32_t>(sig_bit_start) : 0u;
                new_byte = static_cast<uint8_t>(raw_value >> rshift);
                if (sig_bit_start < 0)
                    new_byte = static_cast<uint8_t>(new_byte << (-sig_bit_start));
            }

            uint8_t sig_mask = 0xFF;
            if (bi == last_byte) {
                sig_mask = static_cast<uint8_t>((1u << (msb_in_byte + 1)) - 1);
            }
            if (bi == first_byte && lsb_in_byte > 0) {
                sig_mask &= static_cast<uint8_t>(0xFF << lsb_in_byte);
            }

            data[bi] = (data[bi] & ~sig_mask) | (new_byte & sig_mask);
        }
    }
}

// ============================================================================
// Linear Transformer
// ============================================================================

double CodecEngine::RawToPhysical(uint64_t raw, double factor, double offset,
                                   bool is_signed) {
    if (is_signed)
        return static_cast<double>(static_cast<int64_t>(raw)) * factor + offset;
    return static_cast<double>(raw) * factor + offset;
}

uint64_t CodecEngine::PhysicalToRaw(double physical, double factor, double offset,
                                     bool is_signed, uint32_t bit_length) {
    double raw_d = (physical - offset) / factor;
    double rounded = std::round(raw_d);
    if (is_signed && bit_length > 0 && bit_length < 64) {
        int64_t sval = static_cast<int64_t>(rounded);
        int64_t smin = -(1LL << (bit_length - 1));
        int64_t smax = (1LL << (bit_length - 1)) - 1;
        if (sval > smax) sval = smax;
        if (sval < smin) sval = smin;
        return static_cast<uint64_t>(sval) & ((1ULL << bit_length) - 1);
    }
    if (rounded >= static_cast<double>(UINT64_MAX)) return UINT64_MAX;
    if (rounded <= 0.0) return 0;
    return static_cast<uint64_t>(rounded);
}

// ============================================================================
// DecodeFrame — with Multiplexing Router
// ============================================================================

std::vector<DecodedSignal> CodecEngine::DecodeFrame(
    const Frame& frame, const uint8_t* raw_bytes, size_t size)
{
    std::vector<DecodedSignal> results;
    if (!raw_bytes || size == 0) return results;

    // ── Step 0: PDU header_id routing ────────────────────────────────────
    // If frame has PDUs with non-zero header_id, read 3-byte header_id +
    // 1-byte length from raw data, then only decode the matching PDU.
    bool     has_pdu_routing = false;
    uint32_t active_header_id = 0;
    for (const auto& pdu : frame.pdus) {
        if (pdu.header_id != 0) { has_pdu_routing = true; break; }
    }
    if (has_pdu_routing && size >= 4) {
        active_header_id = (static_cast<uint32_t>(raw_bytes[0]) << 16)
                         | (static_cast<uint32_t>(raw_bytes[1]) << 8)
                         |  static_cast<uint32_t>(raw_bytes[2]);
        // byte 3 = PDU length (informational, not used for decoding)
    }

    // ── Step 1: MUX selector ───────────────────────────────────────────
    uint32_t mux_selector_value = 0;
    bool     has_mux = false;
    for (const auto& sig : frame.signals) {
        if (sig.is_mux_decoder) {
            mux_selector_value = static_cast<uint32_t>(
                ExtractBits(raw_bytes, size, sig.start_bit, sig.bit_length, sig.byte_order));
            has_mux = true; break;
        }
    }

    // ── Step 2: Decode ─────────────────────────────────────────────────
    for (const auto& sig : frame.signals) {
        if (sig.is_mux_decoder) continue;
        if (sig.is_multiplexed && has_mux && sig.mux_value != mux_selector_value) continue;

        bool skip = false;
        uint32_t effective_start = sig.start_bit;

        if (has_pdu_routing) {
            for (const auto& pdu : frame.pdus) {
                for (const auto& ps : pdu.signals) {
                    if (ps.name == sig.name) {
                        if (active_header_id != 0) {
                            if (pdu.header_id == active_header_id)
                                effective_start = sig.start_bit + 32;
                            else
                                skip = true;
                        } else {
                            // active_header_id==0: only decode PDUs with header_id==0
                            if (pdu.header_id != 0) skip = true;
                        }
                        goto found_pdu;
                    }
                }
            }
            // signal is not in any PDU: decode as plain frame signal
            found_pdu:;
        }

        if (skip) continue;

        uint64_t raw = ExtractBits(raw_bytes, size,
                                   effective_start, sig.bit_length, sig.byte_order);
        if (sig.is_signed && sig.bit_length < 64) {
            uint64_t sign_bit = 1ULL << (sig.bit_length - 1);
            if (raw & sign_bit) raw |= ~((1ULL << sig.bit_length) - 1);
        }
        double physical = RawToPhysical(raw, sig.factor, sig.offset, sig.is_signed);
        if (sig.max_value > sig.min_value)
            physical = std::clamp(physical, sig.min_value, sig.max_value);
        results.push_back({sig.name, physical, sig.unit});
    }

    return results;
}

// ============================================================================
// EncodeFrame — with Multiplexing Router
// ============================================================================

bool CodecEngine::EncodeFrame(
    const Frame& frame,
    const std::unordered_map<std::string, double>& signals_to_encode,
    uint8_t* out_bytes, size_t max_size)
{
    if (!out_bytes || max_size == 0) return false;
    std::memset(out_bytes, 0, max_size);

    // ── Step 0: PDU header ──────────────────────────────────────────────
    for (const auto& pdu : frame.pdus) {
        if (pdu.header_id == 0) continue;
        for (const auto& ps : pdu.signals) {
            if (signals_to_encode.count(ps.name)) {
                out_bytes[0] = (pdu.header_id >> 16) & 0xFF;
                out_bytes[1] = (pdu.header_id >> 8) & 0xFF;
                out_bytes[2] = pdu.header_id & 0xFF;
                out_bytes[3] = pdu.byte_length & 0xFF;
                goto header_done;
            }
        }
    }
    header_done:

    // ── Step 1: MUX ─────────────────────────────────────────────────────
    uint32_t mux_sel = 0; bool hm = false;
    for (const auto& s : frame.signals) {
        if (!s.is_mux_decoder) continue;
        auto it = signals_to_encode.find(s.name);
        if (it != signals_to_encode.end()) {
            double p = it->second; if (s.max_value > s.min_value) p = std::clamp(p, s.min_value, s.max_value);
            uint64_t r = PhysicalToRaw(p, s.factor, s.offset, s.is_signed, s.bit_length);
            if (!s.is_signed && s.bit_length < 64) { uint64_t m = (1ULL<<s.bit_length)-1; if(r>m) r=m; }
            PackBits(out_bytes, max_size, s.start_bit, s.bit_length, s.byte_order, r);
            mux_sel = static_cast<uint32_t>(r);
        }
        hm = true; break;
    }

    // ── Step 2: Encode ──────────────────────────────────────────────────
    for (const auto& s : frame.signals) {
        if (s.is_mux_decoder) continue;
        if (s.is_multiplexed && hm && s.mux_value != mux_sel) continue;
        auto it = signals_to_encode.find(s.name);
        if (it == signals_to_encode.end()) continue;
        double p = it->second; if (s.max_value > s.min_value) p = std::clamp(p, s.min_value, s.max_value);
        uint64_t r = PhysicalToRaw(p, s.factor, s.offset, s.is_signed, s.bit_length);
        if (!s.is_signed && s.bit_length < 64) { uint64_t m = (1ULL<<s.bit_length)-1; if(r>m) r=m; }

        uint32_t eff = s.start_bit;
        for (const auto& pd : frame.pdus) {
            if (pd.header_id == 0) continue;
            for (const auto& ps : pd.signals)
                if (ps.name == s.name) { eff = s.start_bit + 32; goto eoff; }
        }
        eoff:;
        PackBits(out_bytes, max_size, eff, s.bit_length, s.byte_order, r);
    }
    return true;
}

} // namespace usde
