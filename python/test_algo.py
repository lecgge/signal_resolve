#!/usr/bin/env python3
"""USDE Algorithm Verification — encode/decode round-trip through Python binding.

Tests verify that the codec engine correctly:
  1. Extracts bits from raw bytes (decode)
  2. Packs bits into raw bytes (encode)
  3. Performs linear transformation (factor/offset)
  4. Handles both Intel and Motorola byte orders
  5. Round-trips encode -> decode correctly
"""

import usde_python

PASS = 0
FAIL = 0

def check(label, condition):
    global PASS, FAIL
    if condition:
        PASS += 1
        print(f"  PASS  {label}")
    else:
        FAIL += 1
        print(f"  FAIL  {label}")

# ─── Load DBC ────────────────────────────────────────────────────────────────

net = usde_python.Network()
ok = net.load_dbc("D:/work/signal_resolve/test_data/main.dbc")
check("LoadDBC succeeds", ok)
check("Frame count = 185", net.frame_count() == 185)

# ─── Test 1: 1-bit Motorola signal ──────────────────────────────────────────
# AMPWorkSta: bit=15, len=1, Motorola -> byte 1, bit 7

print("\n--- Test 1: AMPWorkSta (1-bit Motorola, bit 15) ---")
info = net.frame_info(0x345)
check("Signal name = AMPWorkSta", info["signals"][0]["name"] == "AMPWorkSta")

# Decode: byte 1 bit 7 = 1 -> value=1.0
raw_set   = [0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
raw_clear = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]

dec_set   = net.decode_frame(0x345, raw_set)
dec_clear = net.decode_frame(0x345, raw_clear)
check("Decode bit15=1 -> 1.0", dec_set[0]["value"] == 1.0)
check("Decode bit15=0 -> 0.0", dec_clear[0]["value"] == 0.0)

# Encode round-trip
enc = net.encode_frame(0x345, {"AMPWorkSta": 1.0})
check("Encode -> byte1=0x80", enc[1] == 0x80)
dec_rt = net.decode_frame(0x345, enc)
check("Round-trip -> 1.0", dec_rt[0]["value"] == 1.0)

# ─── Test 2: Multi-byte Motorola, non-overlapping signals ───────────────────
# CCCnstSpdA: bit=12, len=1, Motorola -> byte 1, bit 4
# TrCustSetngDspCmd: bit=15, len=3, Motorola -> byte 1, bits 7..5

print("\n--- Test 2: CCCnstSpdA + TrCustSetngDspCmd (non-overlapping) ---")
enc2 = net.encode_frame(0x20E, {"CCCnstSpdA": 1.0, "TrCustSetngDspCmd": 4.0})
check("CCCnstSpdA=1 -> byte1 bit4 (0x10)", (enc2[1] & 0x10) == 0x10)
# TrCustSetngDspCmd: bit=15, len=3 -> spans byte1 bit7 + byte2 bits 1..0
tr_bits = ((enc2[2] & 0x03) << 1) | ((enc2[1] >> 7) & 1)
check("TrCustSetngDspCmd=4 -> 0b100 across byte1/2", tr_bits == 4)

dec2 = net.decode_frame(0x20E, enc2)
for s in dec2:
    if s["name"] == "CCCnstSpdA":
        check("CCCnstSpdA round-trip == 1.0", s["value"] == 1.0)
    if s["name"] == "TrCustSetngDspCmd":
        check("TrCustSetngDspCmd round-trip == 4.0", s["value"] == 4.0)

# ─── Test 3: Multi-byte with factor/offset ──────────────────────────────────
# RRTireTem: bit=6, len=7, Motorola, factor=2, offset=-60

print("\n--- Test 3: RRTireTem (factor/offset) ---")
info3 = net.frame_info(0x47D)
rr = [s for s in info3["signals"] if s["name"] == "RRTireTem"][0]
check("RRTireTem factor=2.0", rr["factor"] == 2.0)
check("RRTireTem offset=-60.0", rr["offset"] == -60.0)

for temp in [25.0, 80.0, -60.0, 0.0, 150.0]:
    enc3 = net.encode_frame(0x47D, {"RRTireTem": temp})
    dec3 = net.decode_frame(0x47D, enc3)
    for s in dec3:
        if s["name"] == "RRTireTem":
            check(f"RRTireTem={temp} round-trip (within 2C)",
                  abs(s["value"] - temp) < 2.1)

# ─── Test 4: 1-bit signal at different positions ────────────────────────────

print("\n--- Test 4: 1-bit signals at various positions ---")
# CCCnstSpdA: bit=12, len=1 -> byte 1, bit 4
enc4 = net.encode_frame(0x20E, {"CCCnstSpdA": 1.0})
check("CCCnstSpdA=1 -> byte1=0x10", enc4[1] == 0x10)

dec4 = net.decode_frame(0x20E, enc4)
for s in dec4:
    if s["name"] == "CCCnstSpdA":
        check("CCCnstSpdA decode=1.0", s["value"] == 1.0)

# ─── Test 5: Encode zero values ─────────────────────────────────────────────

print("\n--- Test 5: Encode zero values ---")
enc5 = net.encode_frame(0x345, {"AMPWorkSta": 0.0})
check("AMPWorkSta=0 -> byte1=0x00", enc5[1] == 0x00)

# ─── Test 6: All signals in non-overlapping frame ────────────────────────────

print("\n--- Test 6: Frame 0x20E full encode (non-overlapping only) ---")
# Use only non-overlapping signals: ASLSts, CCCnstSpdA, TrCustSetngDspCmd
enc6 = net.encode_frame(0x20E, {
    "ASLSts": 2.0,
    "CCCnstSpdA": 1.0,
    "TrCustSetngDspCmd": 3.0,
})
dec6 = net.decode_frame(0x20E, enc6)
for s in dec6:
    if s["name"] == "ASLSts":
        check("ASLSts=2 round-trip", s["value"] == 2.0)
    if s["name"] == "CCCnstSpdA":
        check("CCCnstSpdA=1 round-trip", s["value"] == 1.0)
    if s["name"] == "TrCustSetngDspCmd":
        check("TrCustSetngDspCmd=3 round-trip", s["value"] == 3.0)

# ─── Summary ─────────────────────────────────────────────────────────────────

print(f"\n{'='*50}")
print(f"  Results: {PASS} PASS, {FAIL} FAIL")
print(f"{'='*50}")
exit(1 if FAIL > 0 else 0)
