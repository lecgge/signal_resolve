#!/usr/bin/env python3
"""USDE Python demo — load a DBC, decode a frame in one line."""

import usde_python

# 1. Create network and load DBC
net = usde_python.Network()
ok = net.load_dbc("test_data/main.dbc")
print(f"load_dbc: {ok}, frames: {net.frame_count()}")

# 2. Show frame info
frame_id = 0x345  # AMP_CFCAN_FrP01
info = net.frame_info(frame_id)
print(f"\nFrame 0x{frame_id:X}: {info['name']}, DLC={info['dlc']}, "
      f"signals={len(info['signals'])}")
for s in info["signals"]:
    print(f"  {s['name']}: bit={s['start_bit']}, len={s['bit_length']}, "
          f"{s['byte_order']}, factor={s['factor']}, offset={s['offset']}")

# 3. Decode — one line
raw = list(bytes([0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]))
decoded = net.decode_frame(frame_id, raw)
print(f"\nDecode frame 0x{frame_id:X}:")
for sig in decoded:
    print(f"  {sig['name']} = {sig['value']} {sig['unit']}")

# 4. Encode round-trip
encoded = net.encode_frame(frame_id, {"AMPWorkSta": 1.0})
print(f"\nEncode frame 0x{frame_id:X}: {' '.join(f'{b:02X}' for b in encoded)}")
