#!/usr/bin/env python3
"""Check LDF frame DLC and signal positions."""
import sys, os

MINGW_BIN = "C:/msys64/mingw64/bin"
if os.path.isdir(MINGW_BIN):
    os.add_dll_directory(MINGW_BIN)

PROJECT = os.path.dirname(os.path.abspath(__file__))
TEST_DATA = os.path.join(PROJECT, "..", "test_data")
PYTHON_DIR = os.path.join(PROJECT, "..", "python")
sys.path.insert(0, PYTHON_DIR)

from usde import Network

net = Network("LDF_Check")
net.load_ldf(os.path.join(TEST_DATA, "Door.ldf"))

ldf_ids = net.frame_ids()
for fid in ldf_ids:
    info = net.frame_info(fid)
    max_sb = max((s.start_bit for s in info.signals), default=0)
    print(f"  Frame 0x{fid:X} ({info.name}): DLC={info.dlc}, "
          f"signals={len(info.signals)}, max_start_bit={max_sb}")
    for s in info.signals:
        needed_bytes = (s.start_bit + s.bit_length) // 8 + 1
        if needed_bytes > info.dlc:
            print(f"    SIGNAL EXCEEDS DLC: {s.name} start_bit={s.start_bit} "
                  f"bit_length={s.bit_length} needs {needed_bytes} bytes, DLC={info.dlc}")