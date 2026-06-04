#include <filesystem>
#include <iostream>
#include <string>

#include "usde_types.h"
#include "dbc_parser.h"
#include "ldf_parser.h"
#include "arxml_parser.h"

static void PrintCluster(const char* label, const usde::NetworkCluster& cl) {
    std::cout << "=== " << label << " ===\n";
    std::cout << "Cluster : " << cl.name << "\n";
    std::cout << "Frames  : " << cl.frames.size() << "\n\n";

    int shown = 0;
    for (auto& [fid, frame] : cl.frames) {
        if (++shown > 10) {
            std::cout << "  ... (" << cl.frames.size() - 10
                      << " more frames)\n\n";
            break;
        }
        std::cout << "  Frame 0x" << std::hex << fid << std::dec
                  << "  [" << frame.name << "]  DLC=" << frame.dlc
                  << "  Signals=" << frame.signals.size() << "\n";
        int sg_shown = 0;
        for (auto& s : frame.signals) {
            if (++sg_shown > 5) {
                std::cout << "    ... (" << frame.signals.size() - 5
                          << " more signals)\n";
                break;
            }
            std::cout << "    SG_ " << s.name
                      << "  bit=" << s.start_bit
                      << "  len=" << s.bit_length
                      << "  " << (s.byte_order == usde::ByteOrder::INTEL
                                      ? "Intel" : "Motorola")
                      << "  factor=" << s.factor
                      << "  offset=" << s.offset
                      << "  [" << s.min_value << ".." << s.max_value << "]"
                      << "  \"" << s.unit << "\"\n";
        }
        std::cout << "\n";
    }
}

int main() {
    namespace fs = std::filesystem;

    fs::path base = "D:/work/signal_resolve/test_data";
    if (!fs::exists(base)) {
        std::cerr << "test_data directory not found at: " << base << "\n";
        return 1;
    }

    // ── DBC ─────────────────────────────────────────────────────────────
    {
        usde::NetworkCluster cluster;
        cluster.name = "CFCAN";
        if (usde::LoadDBC(base / "main.dbc", cluster))
            PrintCluster("DBC Parser (CAN/CAN-FD)", cluster);
        else
            std::cerr << "[ERROR] DBC parsing failed.\n";
    }

    // ── LDF ─────────────────────────────────────────────────────────────
    {
        usde::NetworkCluster cluster;
        cluster.name = "Door_LIN";
        if (usde::LoadLDF(base / "Door.ldf", cluster))
            PrintCluster("LDF Parser (LIN)", cluster);
        else
            std::cerr << "[ERROR] LDF parsing failed.\n";
    }

    // ── ARXML ───────────────────────────────────────────────────────────
    {
        usde::NetworkCluster cluster;
        cluster.name = "AUTOSAR_ZXD";
        if (usde::LoadARXML(base / "test.arxml", cluster))
            PrintCluster("ARXML Parser (AUTOSAR)", cluster);
        else
            std::cerr << "[ERROR] ARXML parsing failed.\n";
    }

    return 0;
}
