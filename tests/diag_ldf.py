#!/usr/bin/env python3
"""Investigate LDF RearWindowsLeft/Right/FrontWindowsLeft decode issues."""
import sys, os

MINGW_BIN = "C:/msys64/mingw64/bin"
if os.path.isdir(MINGW_BIN):
    os.add_dll_directory(MINGW_BIN)

PROJECT = os.path.dirname(os.path.abspath(__file__))
TEST_DATA = os.path.join(PROJECT, "..", "test_data")
PYTHON_DIR = os.path.join(PROJECT, "..", "python")
sys.path.insert(0, PYTHON_DIR)

from usde import Network

net = Network("LDF_Diag")
net.load_ldf(os.path.join(TEST_DATA, "Door.ldf"))

ldf_ids = net.frame_ids()
print(f"LDF frames: {ldf_ids}")
print(f"LDF frame count: {net.frame_count}")

# Find the problematic signals
for fid in ldf_ids:
    info = net.frame_info(fid)
    for s in info.signals:
        if s.name in ("GWI_RearWindowsLeft", "GWI_RearWindowsRight",
                       "GWI_FrontWindowsLeft", "GWI_FrontWindowsRight"):
            print(f"  Signal: {s.name} in frame 0x{fid:X} ({info.name})")
            print(f"    start_bit={s.start_bit}, bit_length={s.bit_length}, "
                  f"byte_order={s.byte_order}, factor={s.factor}, offset={s.offset}")

            # Try encoding and decoding physical=2.0
            enc = net.encode_frame(fid, {s.name: 2.0})
            print(f"    Encode physical=2.0 → raw bytes: {enc}")
            dec = net.decode_frame(fid, enc)
            for ds in dec:
                if ds.name == s.name:
                    print(f"    Decode → {ds.name}={ds.value}")

            # Try physical=3.0
            enc = net.encode_frame(fid, {s.name: 3.0})
            print(f"    Encode physical=3.0 → raw bytes: {enc}")
            dec = net.decode_frame(fid, enc)
            for ds in dec:
                if ds.name == s.name:
                    print(f"    Decode → {ds.name}={ds.value}")

            print(f"    Expected: physical=2 → raw=1, physical=3 → raw=2")
            print()

# Check Intel cross-byte signals in DBC
net_dbc = Network("DBC_Check")
net_dbc.load_dbc(os.path.join(TEST_DATA, "main.dbc"))
ids = net_dbc.frame_ids()
intel_16plus = []
for fid in ids:
    info = net_dbc.frame_info(fid)
    for s in info.signals:
        if s.byte_order == "Intel" and s.bit_length >= 16:
            intel_16plus.append((fid, s.name, s.start_bit, s.bit_length, s.start_bit % 8))

print(f"\nIntel 16+ bit signals found: {len(intel_16plus)}")
for fid, name, sb, bl, sb_mod in intel_16plus[:10]:
    print(f"  0x{fid:X} {name}: start_bit={sb} bit_length={bl} start_bit%8={sb_mod}")