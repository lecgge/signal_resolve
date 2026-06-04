#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace usde {

enum class ByteOrder : uint8_t {
    INTEL = 0,     // Little-Endian
    MOTOROLA = 1   // Big-Endian
};

struct Signal {
    std::string name;
    uint32_t start_bit   = 0;
    uint32_t bit_length  = 0;
    ByteOrder byte_order = ByteOrder::INTEL;
    double factor        = 1.0;
    double offset        = 0.0;
    double min_value     = 0.0;
    double max_value     = 0.0;
    std::string unit;
    bool     is_multiplexed  = false;
    uint32_t mux_value       = 0;
    bool     is_mux_decoder  = false; // MUX selector signal (M flag)
};

struct Frame {
    uint32_t id   = 0;   // CAN ID / LIN PID
    uint32_t dlc  = 0;   // Data Length Code (bytes)
    std::string name;
    std::vector<Signal> signals;
};

struct NetworkCluster {
    std::string name;
    std::unordered_map<uint32_t, Frame> frames;   // Key: Frame ID
};

} // namespace usde
