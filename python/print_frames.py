#!/usr/bin/env python3
"""Print all frames and signals loaded from an ARXML/DBC/LDF file."""

import sys
from usde import Network

def print_all(net, show_all=False):
    print(f"Total frames: {net.frame_count}\n")
    for fid in net.frame_ids():
        info = net.frame_info(fid)
        print(f"Frame 0x{fid:X} [{info.name}]  DLC={info.dlc}  signals={len(info.signals)}")
        sigs = info.signals if show_all else info.signals[:5]
        for s in sigs:
            print(f"  {s.name}: bit={s.start_bit} len={s.bit_length} "
                  f"{s.byte_order} factor={s.factor} offset={s.offset}")
        if not show_all and len(info.signals) > 5:
            print(f"  ... ({len(info.signals)-5} more)")
        print()

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "test_data/test.arxml"
    show_all = "--all" in sys.argv

    net = Network()
    if path.endswith(".dbc"):  net.load_dbc(path)
    elif path.endswith(".ldf"): net.load_ldf(path)
    elif path.endswith(".arxml"): net.load_arxml(path)
    else: raise ValueError(f"Unknown format: {path}")

    print_all(net, show_all)
