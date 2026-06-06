#!/usr/bin/env python3
"""Quick diagnostic for test vector mismatches."""
import sys, os, json

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

print(f"Total vectors: {len(vectors)}")
print(f"Sample vector: frame_id={vectors[0]['frame_id']}, raw_hex={vectors[0]['raw_hex'][:20]}...")
print(f"Expected signals sample: {list(vectors[0]['expected_signals'].items())[:3]}")

# Categorize failures
motorola_fail = 0
intel_fail = 0
motorola_pass = 0
intel_pass = 0
fail_details = []

for v in vectors:
    fid = v["frame_id"]
    raw_hex = v["raw_hex"]
    expected = v["expected_signals"]
    raw_bytes = bytes.fromhex(raw_hex)
    decoded = net.decode_frame(fid, list(raw_bytes))

    info = net.frame_info(fid)

    for sig_name, expected_val in expected.items():
        actual = None
        for s in decoded:
            if s.name == sig_name:
                actual = s.value
                break

        # Find signal info for byte order
        sig_info = None
        for s in info.signals:
            if s.name == sig_name:
                sig_info = s
                break

        if actual is not None and abs(actual - expected_val) <= 1e-3 * max(abs(actual), abs(expected_val), 1.0):
            if sig_info and sig_info.byte_order == "Motorola":
                motorola_pass += 1
            else:
                intel_pass += 1
        else:
            if sig_info and sig_info.byte_order == "Motorola":
                motorola_fail += 1
            else:
                intel_fail += 1
            if len(fail_details) < 20:
                bo = sig_info.byte_order if sig_info else "?"
                sb = sig_info.start_bit if sig_info else "?"
                bl = sig_info.bit_length if sig_info else "?"
                fail_details.append(
                    f"  {sig_name}: bo={bo} start_bit={sb} bit_len={bl} "
                    f"expected={expected_val} actual={actual}"
                )

print(f"\nIntel: {intel_pass} pass, {intel_fail} fail")
print(f"Motorola: {motorola_pass} pass, {motorola_fail} fail")
print(f"\nFirst 20 failures:")
for d in fail_details:
    print(d)