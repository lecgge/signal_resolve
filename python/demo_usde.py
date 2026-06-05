#!/usr/bin/env python3
"""USDE Python demo — show high-level API usage."""

from usde import Network

# 1. Load a DBC database
net = Network(name="CFCAN")
ok = net.load_dbc("test_data/main.dbc")
print(f"Loaded main.dbc: {ok}, frames: {net.frame_count}")

# 2. Inspect a frame
frame_id = 0x345  # AMP_CFCAN_FrP01
info = net.frame_info(frame_id)
print(f"\n{info}")
for sig in info.signals:
    print(f"  {sig}")

# 3. Decode raw CAN bytes — one line
raw = [0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
decoded = net.decode_frame(frame_id, raw)
print(f"\nDecoded 0x{frame_id:X}:")
for sig in decoded:
    print(f"  {sig}")

# 4. Encode — one line
encoded = net.encode_frame(frame_id, {"AMPWorkSta": 1.0})
hex_str = " ".join(f"{b:02X}" for b in encoded)
print(f"\nEncoded AMPWorkSta=1: {hex_str}")
