import sys; sys.path.insert(0, "D:/work/signal_resolve/python")
from usde import Network

net = Network()
net.load_arxml("D:/work/signal_resolve/test_data/test.arxml")

print(f"Frames: {net.frame_count}")
print(f"Clusters: {len(net.clusters())}")
print()
for c in net.clusters():
    print(f"  {c['name']}: {c['bus_type']} {c['baudrate']/1000:.0f}kbps "
          f"FD={c['can_fd']} frames={len(c['frame_ids'])}")
print()

for fid in net.frame_ids()[:8]:
    info = net.frame_info(fid)
    print(f"Frame 0x{fid:X} [{info.name}] DLC={info.dlc}")
    for pdu in info.pdus:
        print(f"  PDU {pdu['name']} ({pdu['byte_length']} bytes, "
              f"start={pdu.get('start_position',0)}, "
              f"header_id={pdu.get('header_id',0)}, "
              f"{len(pdu['signals'])} signals)")
        if pdu.get("uuid"):
            print(f"    uuid={pdu['uuid']}")
        for s in pdu["signals"][:3]:
            print(f"    {s['name']}: bit={s['start_bit']} "
                  f"len={s['bit_length']} {s['byte_order']}")
    print()
