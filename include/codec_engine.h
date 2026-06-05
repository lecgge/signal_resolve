#pragma once

#include "usde_types.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace usde {

struct DecodedSignal {
    std::string name;
    double      physical_value = 0.0;
    std::string unit;
};

class CodecEngine {
public:
    // Decode: raw bytes -> all valid physical values (with MUX routing)
    static std::vector<DecodedSignal> DecodeFrame(
        const Frame&    frame,
        const uint8_t*  raw_bytes,
        size_t          size);

    // Encode: physical values -> packed raw bytes (with MUX routing)
    static bool EncodeFrame(
        const Frame&    frame,
        const std::unordered_map<std::string, double>& signals_to_encode,
        uint8_t*        out_bytes,
        size_t          max_size);

    // ── Low-level bit operations (exposed for unit testing) ──────────────

    // Extract raw integer value from bit stream
    // Intel:     start_bit = LSB position
    // Motorola:  start_bit = MSB position (counted from byte 0, bit 7 downward)
    static uint64_t ExtractBits(
        const uint8_t*  data,
        size_t          size,
        uint32_t        start_bit,
        uint32_t        bit_length,
        ByteOrder       byte_order);

    // Pack raw integer value into bit stream (non-destructive to other bits)
    static void PackBits(
        uint8_t*        data,
        size_t          size,
        uint32_t        start_bit,
        uint32_t        bit_length,
        ByteOrder       byte_order,
        uint64_t        raw_value);

    // Linear conversion: raw -> physical
    static double RawToPhysical(uint64_t raw, double factor, double offset,
                                bool is_signed = false);

    // Linear conversion: physical -> raw (rounded)
    static uint64_t PhysicalToRaw(double physical, double factor, double offset,
                                  bool is_signed = false, uint32_t bit_length = 0);
};

} // namespace usde
