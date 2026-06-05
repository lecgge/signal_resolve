import json
import usde_python

net = usde_python.Network()
net.load_dbc("D:/work/signal_resolve/test_data/main.dbc")

with open("D:/work/signal_resolve/test_data/new_test_date_dbc.json", "r", encoding="utf-8") as f:
    tests = json.load(f)

for tc in tests:
    tid      = tc["id"]
    fid      = tc["frame_id"]
    raw_hex  = tc["raw_hex"]
    expected = tc["expected_signals"]

    raw_bytes = [int(raw_hex[i:i+2], 16) for i in range(0, len(raw_hex), 2)]

    # Decode
    decoded = net.decode_frame(fid, raw_bytes)
    dec_map = {s["name"]: s["value"] for s in decoded}

    # Check decode
    decode_ok = all(dec_map.get(sig) == exp for sig, exp in expected.items())
    if not decode_ok:
        continue  # skip decode failures

    # Encode round-trip
    encoded = net.encode_frame(fid, expected)
    re_decoded = net.decode_frame(fid, encoded)
    rt_map = {s["name"]: s["value"] for s in re_decoded}

    rt_ok = all(rt_map.get(sig) == exp for sig, exp in expected.items())
    if not rt_ok:
        enc_hex = " ".join(f"{b:02X}" for b in encoded)
        print(f"[{tid}] desc={tc['desc']}")
        print(f"  expected: {expected}")
        print(f"  encoded:  {enc_hex}")
        for sig, exp in expected.items():
            act = rt_map.get(sig)
            if act != exp:
                print(f"  FAIL {sig}: exp={exp}, got={act}")
        print()
