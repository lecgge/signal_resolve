#!/usr/bin/env python3
"""USDE Python Package — high-level interface to the native codec engine.

Usage:
    from usde import Network

    net = Network()
    net.load_dbc("test_data/main.dbc")
    net.load_ldf("test_data/Door.ldf")
    net.load_arxml("test_data/test.arxml")

    # Decode raw CAN bytes
    raw = [0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00]
    signals = net.decode_frame(0x1345, raw)
    for sig in signals:
        print(f"{sig.name} = {sig.value} {sig.unit}")

    # Encode physical values
    encoded = net.encode_frame(0x1345, {"CalendarDay": 31.0, "CalendarMonth": 15.0})
    print("Encoded:", " ".join(f"{b:02X}" for b in encoded))
"""

import os as _os
import sys as _sys

# ── Locate and load the native module ────────────────────────────────────────

def _find_native_module():
    """Find usde_python native module (.pyd/.so)."""
    pkg_dir = _os.path.dirname(_os.path.abspath(__file__))

    # 1. Look in the same directory as this __init__.py (copied .pyd)
    if pkg_dir not in _sys.path:
        _sys.path.insert(0, pkg_dir)
    try:
        import usde_python
        return usde_python
    except ImportError:
        pass

    # 2. Try standard import (if installed via pip)
    try:
        import usde_python
        return usde_python
    except ImportError:
        pass

    # 3. Look relative to build directory (development layout)
    for search in [
        _os.path.join(pkg_dir, "..", "..", "build", "Release"),
        _os.path.join(pkg_dir, "..", "..", "build"),
        _os.path.join(pkg_dir, "..", "build", "Release"),
    ]:
        d = _os.path.normpath(search)
        if d not in _sys.path:
            _sys.path.insert(0, d)
        try:
            import usde_python
            return usde_python
        except ImportError:
            pass

    # Build helpful error message
    import sys as _sys2
    py_ver = f"{_sys2.version_info.major}.{_sys2.version_info.minor}"
    pyd_tag = f"cp{_sys2.version_info.major}{_sys2.version_info.minor}"

    # Check what .pyd files are available in the package directory
    available = []
    try:
        for f in _os.listdir(pkg_dir):
            if f.startswith("usde_python.") and (f.endswith(".pyd") or f.endswith(".so")):
                available.append(f)
    except OSError:
        pass

    msg = f"Cannot find usde_python native module for Python {py_ver}.\n\n"
    msg += f"Your Python: {py_ver} (needs *{pyd_tag}*.pyd)\n\n"
    if available:
        msg += "Found these .pyd files in usde/:\n"
        for a in available:
            msg += f"  {a}\n"
        msg += "\nThese are for different Python versions.\n"
        msg += "Rebuild for your Python:\n"
    else:
        msg += "No .pyd files found in usde/ directory.\n"
    msg += "  pip install pybind11\n"
    msg += "  cmake --build build --config Release --target usde_python\n"
    msg += "  Copy build/Release/usde_python*.pyd to usde/"

    raise ImportError(msg)

_native = _find_native_module()

# ── Public API ───────────────────────────────────────────────────────────────

class DecodedSignal:
    """One decoded physical signal."""
    __slots__ = ("name", "value", "unit")
    def __init__(self, name, value, unit=""):
        self.name  = name
        self.value = value
        self.unit  = unit
    def __repr__(self):
        return f"DecodedSignal({self.name}={self.value} {self.unit})"

class SignalInfo:
    """Metadata for one signal definition."""
    __slots__ = ("name", "start_bit", "bit_length", "byte_order",
                 "factor", "offset", "min_value", "max_value", "unit")
    def __init__(self, d):
        self.name       = d["name"]
        self.start_bit  = d["start_bit"]
        self.bit_length = d["bit_length"]
        self.byte_order = d["byte_order"]
        self.factor     = d["factor"]
        self.offset     = d["offset"]
        self.min_value  = d["min_value"]
        self.max_value  = d["max_value"]
        self.unit       = d["unit"]
    def __repr__(self):
        return (f"SignalInfo({self.name}, bit={self.start_bit}, "
                f"len={self.bit_length}, {self.byte_order})")

class FrameInfo:
    """Metadata for one frame."""
    __slots__ = ("id", "name", "dlc", "signals")
    def __init__(self, d, signals):
        self.id      = d["id"]
        self.name    = d["name"]
        self.dlc     = d["dlc"]
        self.signals = signals
    def __repr__(self):
        return (f"FrameInfo(0x{self.id:X}, {self.name}, "
                f"DLC={self.dlc}, {len(self.signals)} signals)")

class Network:
    """USDE signal network loaded from a database file.

    Thread-safe: each Network instance owns independent C++ state.
    """

    def __init__(self, name=""):
        self._net = _native.Network()
        self.name = name

    # ── Database loading ─────────────────────────────────────────────────

    def load_dbc(self, path):
        """Load a CAN/CAN-FD database (DBC format). Returns True on success."""
        return self._net.load_dbc(str(path))

    def load_ldf(self, path):
        """Load a LIN database (LDF format). Returns True on success."""
        return self._net.load_ldf(str(path))

    def load_arxml(self, path):
        """Load an AUTOSAR system description (ARXML format). Returns True on success."""
        return self._net.load_arxml(str(path))

    # ── Query ────────────────────────────────────────────────────────────

    @property
    def frame_count(self):
        """Number of frames loaded."""
        return self._net.frame_count()

    def frame_info(self, frame_id):
        """Get metadata for a single frame."""
        d = self._net.frame_info(frame_id)
        sigs = [SignalInfo(s) for s in d["signals"]]
        return FrameInfo(d, sigs)

    def frame_ids(self):
        """Return list of all loaded frame IDs."""
        ids = []
        # We scan for IDs since the C++ side doesn't expose a list
        # (this is only needed for enumeration, not hot-path decode)
        for fid in range(0x800):  # CAN IDs go up to 0x7FF
            try:
                self._net.frame_info(fid)
                ids.append(fid)
            except Exception:
                pass
        return ids

    # ── Decode / Encode ──────────────────────────────────────────────────

    def decode_frame(self, frame_id, raw_bytes):
        """Decode a frame into physical signal values.

        Args:
            frame_id: CAN/LIN frame ID.
            raw_bytes: Raw bytes as bytes, bytearray, or list of ints.

        Returns:
            List of DecodedSignal objects.
        """
        if isinstance(raw_bytes, (bytes, bytearray)):
            raw_bytes = list(raw_bytes)
        result = self._net.decode_frame(frame_id, raw_bytes)
        return [DecodedSignal(s["name"], s["value"], s["unit"]) for s in result]

    def encode_frame(self, frame_id, signals):
        """Encode physical signal values into raw bytes.

        Args:
            frame_id: CAN/LIN frame ID.
            signals: Dict of {signal_name: physical_value}.

        Returns:
            List of ints (raw bytes).
        """
        return self._net.encode_frame(frame_id, signals)

    def __repr__(self):
        return f"Network({self.name!r}, frames={self.frame_count})"
