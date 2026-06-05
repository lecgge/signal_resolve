#!/usr/bin/env python3
"""Verify PDU header_id encode/decode (chaining) for container frame 0x3F0."""
from usde import Network

net = Network()
net.load_arxml("D:/work/signal_resolve/test_data/test.arxml")
net.set_cluster("ADCANFD")

FID = 0x3F0
info = net.frame_info(FID)
print(f"Frame 0x{FID:X} [{info.name}] DLC={info.dlc}")
print(f"  PDUs: {len(info.pdus)}")
for p in info.pdus:
    print(f"    {p['name']}: header_id={p['header_id']} "
          f"len={p['byte_length']} signals={len(p['signals'])}")

pdu = info.pdus[0]
sig = pdu["signals"][0]
print(f"\n=== Test PDU: {pdu['name']} header_id={pdu['header_id']} ===")
print(f"Signal: {sig['name']} bit={sig['start_bit']} len={sig['bit_length']}")

# Test 1: Encode + decode round-trip
enc = net.encode_frame(FID, {sig['name']: 1.0})
print(f"\nEncode {sig['name']}=1.0:")
print(f"  Raw ({len(enc)}B): {' '.join(f'{b:02X}' for b in enc[:12])}...")

# Verify header
hdr_id_read = (enc[0] << 16) | (enc[1] << 8) | enc[2]
hdr_len_read = enc[3]
print(f"  Header: ID={hdr_id_read} (expect {pdu['header_id']}), "
      f"Len={hdr_len_read} (expect {pdu['byte_length']})")
assert hdr_id_read == pdu["header_id"], f"Header ID mismatch!"
assert hdr_len_read == pdu["byte_length"], f"Header len mismatch!"

# Test 2: Decode round-trip
dec = net.decode_frame(FID, enc)
print(f"\nDecoded {len(dec)} signals (first 5):")
for s in dec[:5]:
    print(f"  {s.name} = {s.value}")
found = any(s.name == sig['name'] and s.value == 1.0 for s in dec)
assert found, f"{sig['name']}=1.0 not found!"
print(f"  --> Round-trip OK")

# Test 3: Wrong header_id should not decode
wrong_raw = [0xFF, 0xFF, 0xFF, 0] + [0] * 60
dec_wrong = net.decode_frame(FID, wrong_raw)
sig_in_wrong = any(s.name == sig['name'] for s in dec_wrong)
assert not sig_in_wrong, "Signal should NOT appear with wrong header_id!"
print(f"  --> Routing OK: wrong header_id -> no {sig['name']} decoded")

# Test 4: Zero value round-trip
enc2 = net.encode_frame(FID, {sig['name']: 0.0})
dec2 = net.decode_frame(FID, enc2)
found2 = any(s.name == sig['name'] and s.value == 0.0 for s in dec2)
assert found2, f"{sig['name']}=0.0 round-trip failed!"
print(f"  --> Zero value OK")

print(f"\n{'='*50}")
print("  All 4 tests PASSED")
print(f"{'='*50}")
