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
        uint32_t shift = (first_byte == 0 && last_byte == 7 && bit_length == 64)
                             ? 0u
                             : static_cast<uint32_t>(first_byte) * 8 + lsb_in_byte;
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
        //   lsb_in_byte = 7 - (start_bit % 8)
        //   first_byte = start_bit / 8
        //   last_byte  = (first_byte * 8 + lsb_in_byte + bit_length - 1) / 8
        //   msb_in_byte = (first_byte * 8 + lsb_in_byte + bit_length - 1) % 8
        //
        uint32_t first_byte   = start_bit / 8;
        uint32_t lsb_in_byte  = 7 - (start_bit % 8);
        uint32_t last_byte    = (first_byte * 8 + lsb_in_byte + bit_length - 1) / 8;
        uint32_t msb_in_byte  = (first_byte * 8 + lsb_in_byte + bit_length - 1) % 8;

        // Place value at correct position in big-endian layout
        uint32_t shift = (first_byte == 0 && last_byte == 7 && bit_length == 64)
                             ? 0u
                             : static_cast<uint32_t>(first_byte) * 8 + lsb_in_byte;

        for (uint32_t bi = first_byte; bi <= last_byte && bi < size; ++bi) {
            uint32_t byte_idx = last_byte - bi;
            uint32_t bit_pos = byte_idx * 8 + shift;
            uint8_t new_byte = (bit_pos < 64)
                                   ? static_cast<uint8_t>(raw_value >> bit_pos)
                                   : 0;

            uint8_t sig_mask = 0xFF;
            if (bi == last_byte) {
                sig_mask = (msb_in_byte == 7) ? 0x80
                          : static_cast<uint8_t>(0xFF << (7 - msb_in_byte));
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

double CodecEngine::RawToPhysical(uint64_t raw, double factor, double offset) {
    return static_cast<double>(raw) * factor + offset;
}

uint64_t CodecEngine::PhysicalToRaw(double physical, double factor, double offset) {
    double raw_d = (physical - offset) / factor;
    // Round to nearest integer, clamp to uint64 range
    double rounded = std::round(raw_d);
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

    // ── Step 1: Find MUX selector value ─────────────────────────────────
    uint32_t mux_selector_value = 0;
    bool     has_mux = false;

    for (const auto& sig : frame.signals) {
        if (sig.is_mux_decoder) {
            mux_selector_value = static_cast<uint32_t>(
                ExtractBits(raw_bytes, size,
                            sig.start_bit, sig.bit_length, sig.byte_order));
            has_mux = true;
            break;
        }
    }

    // ── Step 2: Decode signals with MUX routing ─────────────────────────
    for (const auto& sig : frame.signals) {
        // MUX router: skip decoder signal itself, and skip mux-controlled
        // signals whose mux_value doesn't match the selector
        if (sig.is_mux_decoder) continue;
        if (sig.is_multiplexed && has_mux && sig.mux_value != mux_selector_value)
            continue;

        uint64_t raw = ExtractBits(raw_bytes, size,
                                   sig.start_bit, sig.bit_length, sig.byte_order);

        double physical = RawToPhysical(raw, sig.factor, sig.offset);

        // Clamp to signal range
        if (sig.max_value > sig.min_value)
            physical = std::clamp(physical, sig.min_value, sig.max_value);

        DecodedSignal ds;
        ds.name           = sig.name;
        ds.physical_value = physical;
        ds.unit           = sig.unit;
        results.push_back(std::move(ds));
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

    // Zero-initialize output buffer
    std::memset(out_bytes, 0, max_size);

    // ── Step 1: Find MUX selector and encode it first ───────────────────
    uint32_t mux_selector_value = 0;
    bool     has_mux = false;

    for (const auto& sig : frame.signals) {
        if (!sig.is_mux_decoder) continue;

        auto it = signals_to_encode.find(sig.name);
        if (it != signals_to_encode.end()) {
            double physical = it->second;
            if (sig.max_value > sig.min_value)
                physical = std::clamp(physical, sig.min_value, sig.max_value);

            uint64_t raw = PhysicalToRaw(physical, sig.factor, sig.offset);
            PackBits(out_bytes, max_size,
                     sig.start_bit, sig.bit_length, sig.byte_order, raw);
            mux_selector_value = static_cast<uint32_t>(raw);
        }
        has_mux = true;
        break;
    }

    // ── Step 2: Encode remaining signals with MUX routing ───────────────
    for (const auto& sig : frame.signals) {
        if (sig.is_mux_decoder) continue;
        if (sig.is_multiplexed && has_mux && sig.mux_value != mux_selector_value)
            continue;

        auto it = signals_to_encode.find(sig.name);
        if (it == signals_to_encode.end()) continue;

        double physical = it->second;
        if (sig.max_value > sig.min_value)
            physical = std::clamp(physical, sig.min_value, sig.max_value);

        uint64_t raw = PhysicalToRaw(physical, sig.factor, sig.offset);
        PackBits(out_bytes, max_size,
                 sig.start_bit, sig.bit_length, sig.byte_order, raw);
    }

    return true;
}

} // namespace usde
