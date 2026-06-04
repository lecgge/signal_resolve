#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

#include "usde_types.h"
#include "dbc_parser.h"
#include "ldf_parser.h"
#include "arxml_parser.h"
#include "codec_engine.h"

// ─── Helpers ────────────────────────────────────────────────────────────────

static void PrintCluster(const char* label, const usde::NetworkCluster& cl) {
    std::cout << "=== " << label << " ===\n";
    std::cout << "Cluster : " << cl.name << "\n";
    std::cout << "Frames  : " << cl.frames.size() << "\n\n";
    int shown = 0;
    for (auto& [fid, frame] : cl.frames) {
        if (++shown > 10) {
            std::cout << "  ... (" << cl.frames.size() - 10 << " more frames)\n\n";
            break;
        }
        std::cout << "  Frame 0x" << std::hex << fid << std::dec
                  << "  [" << frame.name << "]  DLC=" << frame.dlc
                  << "  Signals=" << frame.signals.size() << "\n";
        int sg = 0;
        for (auto& s : frame.signals) {
            if (++sg > 5) {
                std::cout << "    ... (" << frame.signals.size() - 5 << " more)\n";
                break;
            }
            std::cout << "    SG_ " << s.name
                      << "  bit=" << s.start_bit << "  len=" << s.bit_length
                      << "  " << (s.byte_order == usde::ByteOrder::INTEL ? "Intel" : "Moto")
                      << "  factor=" << s.factor << "  offset=" << s.offset
                      << "  [" << s.min_value << ".." << s.max_value << "]"
                      << "  \"" << s.unit << "\"\n";
        }
        std::cout << "\n";
    }
}

static void HexDump(const char* label, const uint8_t* data, size_t size) {
    std::cout << "  " << label << ": ";
    for (size_t i = 0; i < size; ++i)
        std::cout << std::hex << std::uppercase << std::setfill('0')
                  << std::setw(2) << static_cast<int>(data[i]) << " ";
    std::cout << std::dec << "\n";
}

static void PrintDecoded(const std::vector<usde::DecodedSignal>& signals) {
    for (auto& ds : signals) {
        std::cout << "    " << ds.name << " = " << ds.physical_value
                  << " " << ds.unit << "\n";
    }
}

// ============================================================================
// Codec Engine Verification Tests
// ============================================================================

static void TestSimpleIntelSignal() {
    std::cout << "--- Test 1: Simple Intel signal (byte-aligned) ---\n";

    usde::Signal sig;
    sig.name      = "Temperature";
    sig.start_bit = 0;
    sig.bit_length = 10;
    sig.byte_order = usde::ByteOrder::INTEL;
    sig.factor    = 0.1;
    sig.offset    = -40.0;
    sig.min_value = -40.0;
    sig.max_value = 185.0;
    sig.unit      = "degC";

    usde::Frame frame;
    frame.id   = 0x100;
    frame.dlc  = 8;
    frame.name = "TestFrame";
    frame.signals.push_back(sig);

    // Encode: 25.5 degC -> raw = round((25.5 - (-40)) / 0.1) = 655
    uint8_t encoded[8] = {};
    std::unordered_map<std::string, double> values = {{"Temperature", 25.5}};
    usde::CodecEngine::EncodeFrame(frame, values, encoded, 8);

    HexDump("Encoded", encoded, 8);
    // raw 655 = 0x028F -> byte0=0x8F, byte1=0x02
    std::cout << "    Expected: 8F 02 00 00 00 00 00 00\n";

    // Decode back
    auto decoded = usde::CodecEngine::DecodeFrame(frame, encoded, 8);
    PrintDecoded(decoded);

    // Verify round-trip
    bool ok = !decoded.empty() && std::abs(decoded[0].physical_value - 25.5) < 0.05;
    std::cout << "    Result: " << (ok ? "PASS" : "FAIL") << "\n\n";
}

static void TestCrossByteIntelSignal() {
    std::cout << "--- Test 2: Cross-byte Intel signal (16-bit) ---\n";

    usde::Signal sig;
    sig.name      = "EngineRPM";
    sig.start_bit = 16;
    sig.bit_length = 16;
    sig.byte_order = usde::ByteOrder::INTEL;
    sig.factor    = 0.25;
    sig.offset    = 0.0;
    sig.min_value = 0.0;
    sig.max_value = 16383.75;
    sig.unit      = "rpm";

    usde::Frame frame;
    frame.id   = 0x200;
    frame.dlc  = 8;
    frame.name = "Engine";
    frame.signals.push_back(sig);

    // Encode: 3000 rpm -> raw = round(3000 / 0.25) = 12000 = 0x2EE0
    uint8_t encoded[8] = {};
    std::unordered_map<std::string, double> values = {{"EngineRPM", 3000.0}};
    usde::CodecEngine::EncodeFrame(frame, values, encoded, 8);

    HexDump("Encoded", encoded, 8);
    // raw 0x2EE0 at bit 16: byte2=0xE0, byte3=0x2E
    std::cout << "    Expected: 00 00 E0 2E 00 00 00 00\n";

    auto decoded = usde::CodecEngine::DecodeFrame(frame, encoded, 8);
    PrintDecoded(decoded);

    bool ok = !decoded.empty() && std::abs(decoded[0].physical_value - 3000.0) < 0.3;
    std::cout << "    Result: " << (ok ? "PASS" : "FAIL") << "\n\n";
}

static void TestCrossByteMotorolaSignal() {
    std::cout << "--- Test 3: Cross-byte Motorola signal (12-bit) ---\n";

    usde::Signal sig;
    sig.name      = "BatteryVoltage";
    sig.start_bit = 15;    // MSB at byte 0, bit 7 (DBC Motorola: bit 15 = byte1.MSB)
    sig.bit_length = 12;
    sig.byte_order = usde::ByteOrder::MOTOROLA;
    sig.factor    = 0.1;
    sig.offset    = 0.0;
    sig.min_value = 0.0;
    sig.max_value = 40.95;
    sig.unit      = "V";

    usde::Frame frame;
    frame.id   = 0x300;
    frame.dlc  = 8;
    frame.name = "Battery";
    frame.signals.push_back(sig);

    // Encode: 14.4V -> raw = round(14.4 / 0.1) = 144 = 0x090
    // Motorola: MSB byte = 15/8 = 1, LSB byte = (15+12-1)/8 = 3
    //   msb_in_byte = 7 - 15%8 = 7-7 = 0, lsb_in_byte = 26%8 = 2
    //   0x090 << 2 = 0x240 -> byte1=0x09, byte2=0x00
    uint8_t encoded[8] = {};
    std::unordered_map<std::string, double> values = {{"BatteryVoltage", 14.4}};
    usde::CodecEngine::EncodeFrame(frame, values, encoded, 8);

    HexDump("Encoded", encoded, 8);
    std::cout << "    Expected: 00 90 00 00 00 00 00 00\n";

    auto decoded = usde::CodecEngine::DecodeFrame(frame, encoded, 8);
    PrintDecoded(decoded);

    bool ok = !decoded.empty() && std::abs(decoded[0].physical_value - 14.4) < 0.05;
    std::cout << "    Result: " << (ok ? "PASS" : "FAIL") << "\n\n";
}

static void TestMultiplexedFrame() {
    std::cout << "--- Test 4: Multiplexed frame (MUX routing) ---\n";

    usde::Frame frame;
    frame.id   = 0x400;
    frame.dlc  = 8;
    frame.name = "MuxFrame";

    // MUX selector (M flag): byte 0, 8 bits
    usde::Signal mux;
    mux.name          = "MUX_ID";
    mux.start_bit     = 0;
    mux.bit_length    = 8;
    mux.byte_order    = usde::ByteOrder::INTEL;
    mux.factor        = 1.0;
    mux.offset        = 0.0;
    mux.is_mux_decoder = true;
    frame.signals.push_back(mux);

    // Regular signal (always present)
    usde::Signal status;
    status.name       = "SystemStatus";
    status.start_bit  = 8;
    status.bit_length = 8;
    status.byte_order = usde::ByteOrder::INTEL;
    status.factor     = 1.0;
    status.offset     = 0.0;
    frame.signals.push_back(status);

    // MUX-controlled signal: only when MUX_ID == 1
    usde::Signal speed;
    speed.name        = "VehicleSpeed";
    speed.start_bit   = 16;
    speed.bit_length  = 16;
    speed.byte_order  = usde::ByteOrder::INTEL;
    speed.factor      = 0.01;
    speed.offset      = 0.0;
    speed.is_multiplexed = true;
    speed.mux_value      = 1;
    frame.signals.push_back(speed);

    // MUX-controlled signal: only when MUX_ID == 2
    usde::Signal pressure;
    pressure.name       = "OilPressure";
    pressure.start_bit  = 16;
    pressure.bit_length = 8;
    pressure.byte_order = usde::ByteOrder::INTEL;
    pressure.factor     = 0.1;
    pressure.offset     = 0.0;
    pressure.is_multiplexed = true;
    pressure.mux_value      = 2;
    frame.signals.push_back(pressure);

    // Test A: MUX=1, Speed=100.00 km/h (raw=10000=0x2710)
    uint8_t dataA[8] = {0x01, 0xAA, 0x10, 0x27, 0x00, 0x00, 0x00, 0x00};
    std::cout << "  Case A: MUX=1\n";
    HexDump("Raw", dataA, 8);
    auto decA = usde::CodecEngine::DecodeFrame(frame, dataA, 8);
    PrintDecoded(decA);
    bool okA = decA.size() == 2; // SystemStatus + VehicleSpeed
    for (auto& d : decA) {
        if (d.name == "VehicleSpeed")
            okA = okA && std::abs(d.physical_value - 100.0) < 0.1;
    }
    std::cout << "    Result: " << (okA ? "PASS" : "FAIL") << "\n\n";

    // Test B: MUX=2, Pressure=3.5 bar (raw=35)
    uint8_t dataB[8] = {0x02, 0xBB, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::cout << "  Case B: MUX=2\n";
    HexDump("Raw", dataB, 8);
    auto decB = usde::CodecEngine::DecodeFrame(frame, dataB, 8);
    PrintDecoded(decB);
    bool okB = decB.size() == 2; // SystemStatus + OilPressure
    for (auto& d : decB) {
        if (d.name == "OilPressure")
            okB = okB && std::abs(d.physical_value - 3.5) < 0.05;
    }
    std::cout << "    Result: " << (okB ? "PASS" : "FAIL") << "\n\n";
}

static void TestEncodeRoundTrip() {
    std::cout << "--- Test 5: Encode -> Decode round-trip (mixed signals) ---\n";

    usde::Frame frame;
    frame.id   = 0x500;
    frame.dlc  = 8;
    frame.name = "MixedFrame";

    // 8-bit Intel, factor=1, offset=0
    usde::Signal s1;
    s1.name = "Flag"; s1.start_bit = 0; s1.bit_length = 8;
    s1.byte_order = usde::ByteOrder::INTEL;
    s1.factor = 1.0; s1.offset = 0.0;
    frame.signals.push_back(s1);

    // 16-bit Intel at bit 8, factor=0.1, offset=-50
    usde::Signal s2;
    s2.name = "Temp"; s2.start_bit = 8; s2.bit_length = 16;
    s2.byte_order = usde::ByteOrder::INTEL;
    s2.factor = 0.1; s2.offset = -50.0;
    s2.min_value = -50.0; s2.max_value = 200.0;
    s2.unit = "degC";
    frame.signals.push_back(s2);

    // 12-bit Motorola at bit 31 (byte 3 bit 7, DBC boundary), factor=0.01, offset=0
    usde::Signal s3;
    s3.name = "Pressure"; s3.start_bit = 31; s3.bit_length = 12;
    s3.byte_order = usde::ByteOrder::MOTOROLA;
    s3.factor = 0.01; s3.offset = 0.0;
    s3.min_value = 0.0; s3.max_value = 40.95;
    s3.unit = "bar";
    frame.signals.push_back(s3);

    // Encode
    std::unordered_map<std::string, double> enc_values = {
        {"Flag", 0xAB},
        {"Temp", 85.0},       // raw = round((85+50)/0.1) = 1350 = 0x0546
        {"Pressure", 2.56},   // raw = round(2.56/0.01) = 256 = 0x100
    };

    uint8_t encoded[8] = {};
    usde::CodecEngine::EncodeFrame(frame, enc_values, encoded, 8);
    HexDump("Encoded", encoded, 8);

    // Decode back
    auto decoded = usde::CodecEngine::DecodeFrame(frame, encoded, 8);
    PrintDecoded(decoded);

    bool ok = (decoded.size() == 3);
    for (auto& d : decoded) {
        if (d.name == "Flag")     ok = ok && std::abs(d.physical_value - 0xAB) < 0.01;
        if (d.name == "Temp")     ok = ok && std::abs(d.physical_value - 85.0) < 0.1;
        if (d.name == "Pressure") ok = ok && std::abs(d.physical_value - 2.56) < 0.02;
    }
    std::cout << "    Result: " << (ok ? "PASS" : "FAIL") << "\n\n";
}

static void TestBoundaryConditions() {
    std::cout << "--- Test 6: Boundary conditions ---\n";

    // Signal at bit 60, length 4 (spans byte 7-8 boundary, but DLC=8 so max byte 7)
    usde::Signal sig;
    sig.name      = "TailBits";
    sig.start_bit = 60;
    sig.bit_length = 4;
    sig.byte_order = usde::ByteOrder::INTEL;
    sig.factor    = 1.0;
    sig.offset    = 0.0;

    usde::Frame frame;
    frame.id = 0x600; frame.dlc = 8; frame.name = "Boundary";
    frame.signals.push_back(sig);

    // Encode value 0xF at bit 60
    uint8_t data[8] = {};
    std::unordered_map<std::string, double> vals = {{"TailBits", 15.0}};
    usde::CodecEngine::EncodeFrame(frame, vals, data, 8);
    HexDump("Encoded", data, 8);

    auto decoded = usde::CodecEngine::DecodeFrame(frame, data, 8);
    PrintDecoded(decoded);

    bool ok = !decoded.empty() && std::abs(decoded[0].physical_value - 15.0) < 0.01;
    std::cout << "    Result: " << (ok ? "PASS" : "FAIL") << "\n\n";

    // Zero-length signal (should not crash)
    usde::Signal zero;
    zero.name = "Zero"; zero.start_bit = 0; zero.bit_length = 0;
    zero.byte_order = usde::ByteOrder::INTEL;
    uint8_t dummy[8] = {};
    uint64_t raw = usde::CodecEngine::ExtractBits(dummy, 8, 0, 0, usde::ByteOrder::INTEL);
    std::cout << "    Zero-length extract: " << raw << " (expected 0)\n";
    std::cout << "    Result: " << (raw == 0 ? "PASS" : "FAIL") << "\n\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    namespace fs = std::filesystem;

    // ── Part 1: Parser Layer Demo ───────────────────────────────────────
    std::cout << "========================================\n";
    std::cout << " USDE — Layer 1: Parser Layer Demo\n";
    std::cout << "========================================\n\n";

    fs::path base = "D:/work/signal_resolve/test_data";
    if (fs::exists(base)) {
        {
            usde::NetworkCluster cl; cl.name = "CFCAN";
            if (usde::LoadDBC(base / "main.dbc", cl))
                PrintCluster("DBC Parser", cl);
        }
        {
            usde::NetworkCluster cl; cl.name = "Door_LIN";
            if (usde::LoadLDF(base / "Door.ldf", cl))
                PrintCluster("LDF Parser", cl);
        }
        {
            usde::NetworkCluster cl; cl.name = "AUTOSAR_ZXD";
            if (usde::LoadARXML(base / "test.arxml", cl))
                PrintCluster("ARXML Parser", cl);
        }
    }

    // ── Part 2: Codec Engine Demo ───────────────────────────────────────
    std::cout << "========================================\n";
    std::cout << " USDE — Layer 2: Codec Engine Tests\n";
    std::cout << "========================================\n\n";

    TestSimpleIntelSignal();
    TestCrossByteIntelSignal();
    TestCrossByteMotorolaSignal();
    TestMultiplexedFrame();
    TestEncodeRoundTrip();
    TestBoundaryConditions();

    std::cout << "========================================\n";
    std::cout << " All tests completed.\n";
    std::cout << "========================================\n";

    return 0;
}
