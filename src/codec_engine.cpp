#include "codec_engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace usde {

// ============================================================================
// Bit-Stream Extractor / Packer
// ============================================================================
//
// Intel (Little-Endian, LSB first):
//   start_bit = position of the signal's LSB.
//   Signal occupies bits [start_bit, start_bit + bit_length - 1].
//   Bytes assembled in little-endian order from LSB byte to MSB byte.
//
// Motorola (Big-Endian, MSB first):
//   DBC start_bit = position of the signal's MSB.
//   MSB_byte = start_bit / 8,  MSB_std_bit = start_bit % 8
//   The MSB (highest value bit) is at byte MSB_byte, std bit MSB_std_bit.
//   The signal extends from MSB_std_bit down to std 0 within MSB_byte,
//   then continues in subsequent bytes (MSB_byte+1, etc.) with full 8-bit
//   occupancy, ending in LSB_byte where it may occupy only the top bits.
//
//   Key formulas for multi-byte Motorola:
//     bits_in_msb  = start_bit % 8 + 1
//     remaining    = bit_length - bits_in_msb
//     total_bytes  = 1 + (remaining + 7) / 8   (when remaining > 0)
//     LSB_byte     = MSB_byte + total_bytes - 1
//     bits_in_lsb  = (remaining % 8) ? remaining % 8 : 8
//     shift        = (8 - bits_in_lsb) % 8
//
//   Bytes are assembled in big-endian order: MSB_byte first (highest value),
//   LSB_byte last (lowest value). After assembly, shift right by `shift`
//   and mask to bit_length bits.
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
        // ── Motorola: start_bit = MSB, big-endian byte order ─────────
        //
        // MSB is at byte (start_bit/8), std bit (start_bit%8).
        // Signal extends from std bit (start_bit%8) down to std 0 within
        // the MSB byte, then fills subsequent bytes until bit_length is covered.
        //
        uint32_t msb_byte    = start_bit / 8;
        uint32_t msb_std_bit = start_bit % 8;
        uint32_t bits_in_msb = msb_std_bit + 1;   // bits occupied in MSB byte

        if (msb_byte >= size) return 0;

        // Single-byte: signal fits entirely in MSB byte
        if (bits_in_msb >= bit_length) {
            uint32_t bit_pos = msb_std_bit - bit_length + 1;
            uint64_t val = data[msb_byte] >> bit_pos;
            uint64_t mask = (bit_length == 64) ? ~uint64_t(0)
                                               : ((uint64_t(1) << bit_length) - 1);
            return val & mask;
        }

        // Multi-byte: signal spans MSB_byte .. LSB_byte
        uint32_t remaining    = bit_length - bits_in_msb;
        uint32_t total_bytes  = 1 + (remaining + 7) / 8;
        uint32_t lsb_byte     = msb_byte + total_bytes - 1;
        uint32_t bits_in_lsb  = (remaining % 8) ? (remaining % 8) : 8;
        uint32_t shift        = (8 - bits_in_lsb) % 8;

        if (lsb_byte >= size) return 0;

        // Assemble bytes from MSB_byte to LSB_byte in big-endian order
        uint64_t val = 0;
        uint32_t nbytes = (total_bytes > 8) ? 8 : total_bytes;
        for (uint32_t i = 0; i < nbytes; ++i) {
            uint32_t bi = msb_byte + i;
            if (bi < size)
                val = (val << 8) | data[bi];
        }

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
        // ── Motorola: start_bit = MSB, big-endian byte order ─────────
        //
        // MSB is at byte (start_bit/8), std bit (start_bit%8).
        // Signal extends from std bit (start_bit%8) down to std 0 within
        // the MSB byte, then fills subsequent bytes until bit_length is covered.
        //
        uint32_t msb_byte    = start_bit / 8;
        uint32_t msb_std_bit = start_bit % 8;
        uint32_t bits_in_msb = msb_std_bit + 1;

        if (msb_byte >= size) return;

        // Single-byte: signal fits entirely in MSB byte
        if (bits_in_msb >= bit_length) {
            uint32_t bit_pos = msb_std_bit - bit_length + 1;
            uint64_t m = (bit_length == 64) ? ~0ULL : ((1ULL << bit_length) - 1);
            uint8_t  sig_mask = static_cast<uint8_t>(m << bit_pos);
            uint8_t  val      = static_cast<uint8_t>((raw_value & m) << bit_pos);
            data[msb_byte] = (data[msb_byte] & ~sig_mask) | val;
            return;
        }

        // Multi-byte: signal spans MSB_byte .. LSB_byte
        uint32_t remaining    = bit_length - bits_in_msb;
        uint32_t total_bytes  = 1 + (remaining + 7) / 8;
        uint32_t lsb_byte     = msb_byte + total_bytes - 1;
        uint32_t bits_in_lsb  = (remaining % 8) ? (remaining % 8) : 8;
        uint32_t shift        = (8 - bits_in_lsb) % 8;

        if (lsb_byte >= size) return;

        uint64_t val_shifted = raw_value << shift;

        // Iterate from LSB_byte (j=0, lowest value bits) up to MSB_byte
        // (j=total_bytes-1, highest value bits).
        // In big-endian layout: byte at index j from LSB has assembled
        // value bits [j*8, j*8+7].
        for (uint32_t j = 0; j < total_bytes; ++j) {
            uint32_t bi = lsb_byte - j;
            if (bi >= size) continue;

            uint8_t new_byte = static_cast<uint8_t>(val_shifted >> (j * 8));

            uint8_t sig_mask = 0xFF;
            if (j == total_bytes - 1) {
                // MSB byte: signal occupies std bits [0 .. msb_std_bit]
                sig_mask = static_cast<uint8_t>((1u << (msb_std_bit + 1)) - 1);
            }
            if (j == 0 && bits_in_lsb < 8) {
                // LSB byte: signal occupies std bits [8-bits_in_lsb .. 7]
                sig_mask &= static_cast<uint8_t>(((1u << bits_in_lsb) - 1) << (8 - bits_in_lsb));
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
// DecodeFrame — with PDU chaining (3+1+len) and MUX routing
// ============================================================================

std::vector<DecodedSignal> CodecEngine::DecodeFrame(
    const Frame& frame, const uint8_t* raw_bytes, size_t size)
{
    std::vector<DecodedSignal> results;
    if (!raw_bytes || size == 0) return results;

    // ── Step 0: PDU header_id routing (3+1+len chaining) ──────────────────
    // If frame has PDUs with non-zero header_id, parse raw bytes as a
    // sequence of [3B header_id][1B length][N bytes payload] chunks.
    bool has_pdu_routing = false;
    for (const auto& pdu : frame.pdus) {
        if (pdu.header_id != 0) { has_pdu_routing = true; break; }
    }

    if (has_pdu_routing) {
        // Determine the byte offset where 3+1+len PDU data starts in the frame.
        // All routing PDUs share the same start_position (inherited from container).
        size_t pdu_base = 0;
        for (const auto& pdu : frame.pdus) {
            if (pdu.header_id != 0) {
                pdu_base = pdu.start_position;
                break;
            }
        }

        size_t offset = pdu_base;
        while (offset + 4 <= size) {
            uint32_t chunk_header_id = (static_cast<uint32_t>(raw_bytes[offset]) << 16)
                                     | (static_cast<uint32_t>(raw_bytes[offset + 1]) << 8)
                                     |  static_cast<uint32_t>(raw_bytes[offset + 2]);
            // byte[3] = PDU total length (or 0 as stop marker)

            size_t data_start = offset + 4;
            size_t advance    = 4 + static_cast<size_t>(raw_bytes[offset + 3]); // use byte[3] length

            for (const auto& pdu : frame.pdus) {
                if (pdu.header_id == 0) continue;
                if (pdu.header_id != chunk_header_id) continue;

                // Use PDU's declared byte_length for advance (more reliable than raw byte[3])
                advance = 4 + pdu.byte_length; // header(4) + data(byte_length)

                for (const auto& sig : pdu.signals) {
                    uint32_t effective_start = sig.start_bit
                        + static_cast<uint32_t>(data_start) * 8;
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
                break;
            }

            offset += advance;
            if (advance <= 4) break; // stop marker (byte[3]==0) or zero-length PDU — end chain
        }

        // Also decode signals from static PDUs (header_id == 0) that occupy
        // fixed byte positions within the frame (e.g., alongside a CONTAINER-I-PDU).
        for (const auto& pdu : frame.pdus) {
            if (pdu.header_id != 0) continue;  // already decoded via routing
            for (const auto& sig : pdu.signals) {
                uint32_t effective_start = sig.start_bit + pdu.start_position * 8;
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
        }

        return results;
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

    // ── Step 2: Decode frame-level signals (DBC/LDF style) ─────────────
    for (const auto& sig : frame.signals) {
        if (sig.is_mux_decoder) continue;
        if (sig.is_multiplexed && has_mux && sig.mux_value != mux_selector_value) continue;

        uint64_t raw = ExtractBits(raw_bytes, size,
                                   sig.start_bit, sig.bit_length, sig.byte_order);
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
// EncodeFrame — with PDU chaining (3+1+len) and MUX routing
// ============================================================================

bool CodecEngine::EncodeFrame(
    const Frame& frame,
    const std::unordered_map<std::string, double>& signals_to_encode,
    uint8_t* out_bytes, size_t max_size)
{
    if (!out_bytes || max_size == 0) return false;
    std::memset(out_bytes, 0, max_size);

    // ── Step 0: PDU routing (3+1+len chaining) ────────────────────────────
    bool has_pdu_routing = false;
    for (const auto& pdu : frame.pdus) {
        if (pdu.header_id != 0) { has_pdu_routing = true; break; }
    }

    if (has_pdu_routing) {
        // Determine the byte offset where 3+1+len PDU data starts in the frame.
        size_t pdu_base = 0;
        for (const auto& pdu : frame.pdus) {
            if (pdu.header_id != 0) {
                pdu_base = pdu.start_position;
                break;
            }
        }

        size_t offset = pdu_base;
        for (const auto& pdu : frame.pdus) {
            if (pdu.header_id == 0) continue;

            bool has_signals = false;
            for (const auto& sig : pdu.signals) {
                if (signals_to_encode.count(sig.name)) { has_signals = true; break; }
            }
            if (!has_signals) continue;

            // PDU total = 4-byte header + byte_length data
            if (offset + 4 + pdu.byte_length > max_size) break;

            out_bytes[offset]     = (pdu.header_id >> 16) & 0xFF;
            out_bytes[offset + 1] = (pdu.header_id >> 8) & 0xFF;
            out_bytes[offset + 2] = pdu.header_id & 0xFF;
            out_bytes[offset + 3] = pdu.byte_length & 0xFF;

            for (const auto& sig : pdu.signals) {
                auto it = signals_to_encode.find(sig.name);
                if (it == signals_to_encode.end()) continue;
                double p = it->second;
                if (sig.max_value > sig.min_value)
                    p = std::clamp(p, sig.min_value, sig.max_value);
                uint64_t r = PhysicalToRaw(p, sig.factor, sig.offset,
                                           sig.is_signed, sig.bit_length);
                if (!sig.is_signed && sig.bit_length < 64) {
                    uint64_t m = (1ULL << sig.bit_length) - 1;
                    if (r > m) r = m;
                }
                uint32_t eff = sig.start_bit
                    + static_cast<uint32_t>(offset + 4) * 8;
                PackBits(out_bytes, max_size, eff,
                         sig.bit_length, sig.byte_order, r);
            }
            offset += 4 + pdu.byte_length;
        }

        // Also encode signals from static PDUs (header_id == 0)
        // at their fixed byte positions within the frame.
        for (const auto& pdu : frame.pdus) {
            if (pdu.header_id != 0) continue;  // already encoded via routing
            for (const auto& sig : pdu.signals) {
                auto it = signals_to_encode.find(sig.name);
                if (it == signals_to_encode.end()) continue;
                double p = it->second;
                if (sig.max_value > sig.min_value)
                    p = std::clamp(p, sig.min_value, sig.max_value);
                uint64_t r = PhysicalToRaw(p, sig.factor, sig.offset,
                                           sig.is_signed, sig.bit_length);
                if (!sig.is_signed && sig.bit_length < 64) {
                    uint64_t m = (1ULL << sig.bit_length) - 1;
                    if (r > m) r = m;
                }
                uint32_t eff = sig.start_bit + pdu.start_position * 8;
                PackBits(out_bytes, max_size, eff,
                         sig.bit_length, sig.byte_order, r);
            }
        }

        return true;
    }

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

    // ── Step 2: Encode frame-level signals (DBC/LDF style) ─────────────
    for (const auto& s : frame.signals) {
        if (s.is_mux_decoder) continue;
        if (s.is_multiplexed && hm && s.mux_value != mux_sel) continue;
        auto it = signals_to_encode.find(s.name);
        if (it == signals_to_encode.end()) continue;
        double p = it->second; if (s.max_value > s.min_value) p = std::clamp(p, s.min_value, s.max_value);
        uint64_t r = PhysicalToRaw(p, s.factor, s.offset, s.is_signed, s.bit_length);
        if (!s.is_signed && s.bit_length < 64) { uint64_t m = (1ULL<<s.bit_length)-1; if(r>m) r=m; }
        PackBits(out_bytes, max_size, s.start_bit, s.bit_length, s.byte_order, r);
    }
    return true;
}

} // namespace usde
