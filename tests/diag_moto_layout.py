#!/usr/bin/env python3
"""Deep diagnostic: examine DTCInfomationFICM test vectors and determine
correct Motorola bit layout."""
import sys, os, json, struct

MINGW_BIN = "C:/msys64/mingw64/bin"
if os.path.isdir(MINGW_BIN):
    os.add_dll_directory(MINGW_BIN)

PROJECT = os.path.dirname(os.path.abspath(__file__))
TEST_DATA = os.path.join(PROJECT, "..", "test_data")
PYTHON_DIR = os.path.join(PROJECT, "..", "python")
sys.path.insert(0, PYTHON_DIR)

from usde import Network

net = Network("DBC_Diag")
net.load_dbc(os.path.join(TEST_DATA, "main.dbc"))

json_path = os.path.join(TEST_DATA, "dbc_test_date.json")
with open(json_path, "r", encoding="utf-8") as f:
    vectors = json.load(f)

# Find all test vectors that include DTCInfomationFICM
dtc_vectors = []
for v in vectors:
    if "DTCInfomationFICM" in v["expected_signals"]:
        dtc_vectors.append(v)

print(f"Found {len(dtc_vectors)} test vectors with DTCInfomationFICM")

# Examine the first few
for i, v in enumerate(dtc_vectors[:5]):
    fid = v["frame_id"]
    raw_hex = v["raw_hex"]
    expected = v["expected_signals"]["DTCInfomationFICM"]
    raw_bytes = bytes.fromhex(raw_hex)

    print(f"\n--- Vector {i}: frame_id={fid} ---")
    print(f"  raw_hex: {raw_hex}")
    print(f"  raw_bytes: {list(raw_bytes)}")
    print(f"  expected DTCInfomationFICM = {expected}")

    # Get signal info
    info = net.frame_info(fid)
    dtc_sig = None
    for s in info.signals:
        if s.name == "DTCInfomationFICM":
            dtc_sig = s
            break

    if dtc_sig:
        print(f"  signal: start_bit={dtc_sig.start_bit}, bit_length={dtc_sig.bit_length}, "
              f"byte_order={dtc_sig.byte_order}, factor={dtc_sig.factor}, offset={dtc_sig.offset}")

    # Try different byte range interpretations
    S = dtc_sig.start_bit  # 7
    L = dtc_sig.bit_length  # 56

    # Interpretation A: Reversed numbering, signal extends to higher reversed bits
    msb_byte_a = S // 8
    lsb_byte_a = (S + L - 1) // 8

    val_a = 0
    for bi in range(msb_byte_a, lsb_byte_a + 1):
        if bi < len(raw_bytes):
            val_a = (val_a << 8) | raw_bytes[bi]
    shift_a = 7 - ((S + L - 1) % 8)
    val_a_shifted = val_a >> shift_a
    val_a_masked = val_a_shifted & ((1 << L) - 1)
    phys_a = val_a_masked * dtc_sig.factor + dtc_sig.offset
    print(f"  Interpretation A (reversed, big-endian 0-7, shift=1): raw={val_a_masked}, phys={phys_a}")

    # Interpretation B: Old code - bytes 0-6, shift=0
    val_b = 0
    for bi in range(0, 7):
        if bi < len(raw_bytes):
            val_b = (val_b << 8) | raw_bytes[bi]
    val_b_shifted = val_b >> 0
    val_b_masked = val_b_shifted & ((1 << L) - 1)
    phys_b = val_b_masked * dtc_sig.factor + dtc_sig.offset
    print(f"  Interpretation B (old code, big-endian 0-6, shift=0): raw={val_b_masked}, phys={phys_b}")

    # Interpretation C: Forward numbering, bytes 0-6, little-endian
    val_c = 0
    for bi in range(0, 7):
        if bi < len(raw_bytes):
            val_c |= raw_bytes[bi] << (bi * 8)
    val_c_shifted = val_c >> 0
    val_c_masked = val_c_shifted & ((1 << L) - 1)
    phys_c = val_c_masked * dtc_sig.factor + dtc_sig.offset
    print(f"  Interpretation C (forward, little-endian 0-6, shift=0): raw={val_c_masked}, phys={phys_c}")

    # Interpretation D: direct reversed bit extraction
    val_direct = 0
    for N in range(S, S + L):
        byte_idx = N // 8
        std_bit = 7 - (N % 8)
        value_bit = S + L - 1 - N
        if byte_idx < len(raw_bytes):
            bit_val = (raw_bytes[byte_idx] >> std_bit) & 1
            val_direct |= bit_val << value_bit
    phys_direct = val_direct * dtc_sig.factor + dtc_sig.offset
    print(f"  Interpretation D (direct reversed bit extraction): raw={val_direct}, phys={phys_direct}")

    # Our current decode
    decoded = net.decode_frame(fid, list(raw_bytes))
    for s in decoded:
        if s.name == "DTCInfomationFICM":
            print(f"  Current codec decode: {s.value}")
            break

    print(f"  Expected: {expected}")
    print(f"  Match A: {abs(phys_a - expected) < 0.01}")
    print(f"  Match B: {abs(phys_b - expected) < 0.01}")
    print(f"  Match C: {abs(phys_c - expected) < 0.01}")
    print(f"  Match D: {abs(phys_direct - expected) < 0.01}")

# Also check a few other Motorola signals
print("\n\n=== Checking other Motorola signals ===")
other_moto_fail = 0
other_moto_pass = 0
other_moto_names = set()
for v in vectors:
    fid = v["frame_id"]
    raw_hex = v["raw_hex"]
    expected = v["expected_signals"]
    raw_bytes = bytes.fromhex(raw_hex)
    decoded = net.decode_frame(fid, list(raw_bytes))
    info = net.frame_info(fid)

    for sig_name, expected_val in expected.items():
        if sig_name == "DTCInfomationFICM":
            continue
        sig_info = None
        for s in info.signals:
            if s.name == sig_name:
                sig_info = s
                break
        if sig_info and sig_info.byte_order == "Motorola":
            actual = None
            for s in decoded:
                if s.name == sig_name:
                    actual = s.value
                    break
            if actual is not None and abs(actual - expected_val) < 0.01 * max(abs(actual), abs(expected_val), 1.0):
                other_moto_pass += 1
            else:
                other_moto_fail += 1
                other_moto_names.add(sig_name)

print(f"Other Motorola signals: {other_moto_pass} pass, {other_moto_fail} fail")
if other_moto_names:
    print(f"Failing signal names: {other_moto_names}")