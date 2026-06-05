#!/usr/bin/env python3
"""Verify PDU header_id encode/decode for ARXML frame 0x4C0."""

from usde import Network

net = Network()
net.load_arxml("D:/work/signal_resolve/test_data/test.arxml")

FID = 0x4C0
info = net.frame_info(FID)
print(f"Frame 0x{FID:X} [{info.name}] DLC={info.dlc}")
print(f"  PDUs: {len(info.pdus)}")
for p in info.pdus:
    print(f"    {p['name']}: header_id={p['header_id']} "
          f"len={p['byte_length']} signals={len(p['signals'])}")

# Pick the first PDU (header_id=79875)
pdu = info.pdus[0]
print(f"\n=== Test PDU: {pdu['name']} header_id={pdu['header_id']} ===")
sig_name = pdu["signals"][0]["name"]
print(f"Signal: {sig_name} bit={pdu['signals'][0]['start_bit']} "
      f"len={pdu['signals'][0]['bit_length']}")

# Test 1: Encode
enc = net.encode_frame(FID, {sig_name: 1.0})
print(f"\nEncode {sig_name}=1.0:")
print(f"  Raw: {' '.join(f'{b:02X}' for b in enc)}")

# Verify header bytes
import struct
hdr_id_read = (enc[0] << 16) | (enc[1] << 8) | enc[2]
hdr_len_read = enc[3]
print(f"  Header: ID={hdr_id_read} (expect {pdu['header_id']}), "
      f"Len={hdr_len_read} (expect {pdu['byte_length']})")
assert hdr_id_read == pdu["header_id"], f"Header ID mismatch!"
assert hdr_len_read == pdu["byte_length"], f"Header len mismatch!"

# Test 2: Decode the encoded bytes
dec = net.decode_frame(FID, enc)
print(f"\nDecode encoded bytes:")
for s in dec:
    print(f"  {s.name} = {s.value} {s.unit}")

# Verify decode
found = any(s.name == sig_name and s.value == 1.0 for s in dec)
assert found, f"{sig_name}=1.0 not found in decode!"
print(f"  --> Round-trip OK: encode({sig_name}=1) -> decode -> 1")

# Test 3: Decode with wrong header_id should NOT get this PDU's signals
wrong_raw = [0xFF, 0xFF, 0xFF, 8] + [0]*4  # header_id=0xFFFFFF (no match)
dec_wrong = net.decode_frame(FID, wrong_raw)
sig_in_wrong = any(s.name == sig_name for s in dec_wrong)
assert not sig_in_wrong, "Signal should NOT appear with wrong header_id!"
print(f"\n  --> Routing OK: wrong header_id=0xFFFFFF -> no {sig_name} decoded")

# Test 4: zero value signal
enc2 = net.encode_frame(FID, {sig_name: 0.0})
dec2 = net.decode_frame(FID, enc2)
found2 = any(s.name == sig_name and s.value == 0.0 for s in dec2)
assert found2, f"{sig_name}=0.0 round-trip failed!"
print(f"  --> Zero value OK: encode(0) -> decode -> 0")

# Test 5: second PDU in same frame
pdu2 = info.pdus[1]
sig2 = pdu2["signals"][0]["name"]
enc3 = net.encode_frame(FID, {sig2: 1.0})
hdr2 = (enc3[0] << 16) | (enc3[1] << 8) | enc3[2]
assert hdr2 == pdu2["header_id"], f"PDU2 header mismatch: {hdr2} != {pdu2['header_id']}"
dec3 = net.decode_frame(FID, enc3)
found3 = any(s.name == sig2 and s.value == 1.0 for s in dec3)
assert found3, f"{sig2}=1.0 round-trip failed!"
# Should NOT decode PDU1's signal
has_pdu1 = any(s.name == sig_name for s in dec3)
assert not has_pdu1, f"PDU1 signal appeared in PDU2 decode!"
print(f"  --> Multi-PDU OK: encode({sig2}=1) uses header_id={pdu2['header_id']}, "
      f"routing correct")

print(f"\n{'='*50}")
print("  All 5 tests PASSED")
print(f"{'='*50}")
