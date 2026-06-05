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
    bool     is_mux_decoder  = false;
    bool     is_signed       = false;
};

struct Pdu {
    std::string name;
    uint32_t    byte_length = 0;
    std::vector<Signal> signals;
};

struct Frame {
    uint32_t id   = 0;
    uint32_t dlc  = 0;
    std::string name;
    std::vector<Signal> signals;   // codec uses this (from DBC/LDF or flattened PDU)
    std::vector<Pdu>    pdus;      // PDU hierarchy (ARXML only)
};

struct Cluster {
    std::string name;
    std::string bus_type;          // "CAN", "CAN-FD", "LIN"
    uint32_t    baudrate = 0;
    bool        can_fd    = false;
    std::unordered_map<uint32_t, Frame> frames;
};

struct NetworkCluster {
    std::string name;
    std::string bus_type;
    uint32_t    baudrate = 0;
    bool        can_fd   = false;
    std::unordered_map<uint32_t, Frame> frames;           // Key: Frame ID
    std::unordered_map<std::string, Cluster> clusters;    // ARXML only
};

} // namespace usde
