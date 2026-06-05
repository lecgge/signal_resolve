package com.usde;

import com.sun.jna.Memory;
import com.sun.jna.Pointer;

import java.io.Closeable;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * High-level Java client for USDE signal decoding engine.
 *
 * <p>Wraps the JNA {@link UsdeLibrary} interface, hiding Pointer types
 * and native memory management behind a clean Java API.
 *
 * <p>Thread-safe: each instance owns independent native state.
 *
 * <p>Usage:
 * <pre>{@code
 *   try (UsdeClient client = new UsdeClient()) {
 *       client.loadDbc("test_data/main.dbc");
 *       byte[] raw = new byte[]{0x00, 0x80, 0, 0, 0, 0, 0, 0};
 *       DecodedSignal[] sigs = client.decodeFrame(0x345, raw);
 *       for (DecodedSignal s : sigs) {
 *           System.out.println(s.name + " = " + s.value + " " + s.unit);
 *       }
 *   }
 * }</pre>
 */
public class UsdeClient implements Closeable {

    /** A single decoded physical signal. */
    public static class DecodedSignal {
        public final String name;
        public final double value;
        public final String unit;

        DecodedSignal(String name, double value, String unit) {
            this.name  = name;
            this.value = value;
            this.unit  = unit != null ? unit : "";
        }

        @Override
        public String toString() {
            return name + " = " + value + " " + unit;
        }
    }

    /** Metadata for a frame. */
    public static class FrameInfo {
        public final int    id;
        public final String name;
        public final int    dlc;
        public final int    signalCount;

        FrameInfo(int id, String name, int dlc, int signalCount) {
            this.id          = id;
            this.name        = name;
            this.dlc         = dlc;
            this.signalCount = signalCount;
        }

        @Override
        public String toString() {
            return String.format("Frame 0x%X [%s] DLC=%d %d signals",
                    id, name, dlc, signalCount);
        }
    }

    private final UsdeLibrary lib;
    private Pointer handle;

    // ─── Construction ─────────────────────────────────────────────────

    /**
     * Create a new USDE client backed by the native library.
     * The native shared library (usde.dll / libusde.so) must be on
     * {@code java.library.path} or in the system library search path.
     */
    public UsdeClient() {
        lib    = UsdeLibrary.INSTANCE;
        handle = lib.USDE_CreateNetwork();
        if (handle == null)
            throw new RuntimeException("USDE: failed to create native network handle");
    }

    /**
     * Create a client using a specific native library instance.
     * Useful when the library is loaded from a custom location.
     */
    public UsdeClient(UsdeLibrary library) {
        this.lib    = library;
        this.handle = library.USDE_CreateNetwork();
        if (handle == null)
            throw new RuntimeException("USDE: failed to create native network handle");
    }

    // ─── Database loading ──────────────────────────────────────────────

    /** Load a CAN/CAN-FD DBC file. Returns true on success. */
    public boolean loadDbc(String path) {
        return lib.USDE_LoadDBC(handle, path) == 1;
    }

    /** Load a LIN LDF file. Returns true on success. */
    public boolean loadLdf(String path) {
        return lib.USDE_LoadLDF(handle, path) == 1;
    }

    /** Load an AUTOSAR ARXML file. Returns true on success. */
    public boolean loadArxml(String path) {
        return lib.USDE_LoadARXML(handle, path) == 1;
    }

    // ─── Query ─────────────────────────────────────────────────────────

    /** Number of frames currently loaded. */
    public int getFrameCount() {
        return lib.USDE_GetFrameCount(handle);
    }

    // ─── Decode ────────────────────────────────────────────────────────

    /**
     * Decode one frame from raw CAN/LIN bytes.
     *
     * @param frameId  Frame ID (CAN ID or LIN PID).
     * @param rawBytes Raw 8-byte CAN frame data.
     * @return Array of decoded physical signals (never null).
     */
    public DecodedSignal[] decodeFrame(int frameId, byte[] rawBytes) {
        int maxSignals = 128;
        UsdeLibrary.C_DecodedSignal[] nativeSignals =
                (UsdeLibrary.C_DecodedSignal[]) new UsdeLibrary.C_DecodedSignal()
                        .toArray(maxSignals);

        Pointer rawPtr = new Memory(rawBytes.length);
        rawPtr.write(0, rawBytes, 0, rawBytes.length);

        int[] outCount = new int[1];
        int rc = lib.USDE_DecodeFrame(handle, frameId,
                rawPtr, rawBytes.length,
                nativeSignals, maxSignals, outCount);

        if (rc != 1 || outCount[0] == 0)
            return new DecodedSignal[0];

        DecodedSignal[] result = new DecodedSignal[outCount[0]];
        for (int i = 0; i < outCount[0]; i++) {
            nativeSignals[i].read();
            result[i] = new DecodedSignal(
                    nativeSignals[i].getNameString(),
                    nativeSignals[i].physicalValue,
                    nativeSignals[i].getUnitString());
        }
        return result;
    }

    // ─── Encode ────────────────────────────────────────────────────────

    /**
     * Encode physical signal values into raw CAN bytes.
     *
     * @param frameId Frame ID.
     * @param signals Map of signal name to physical value.
     * @return Encoded raw bytes (DLC length).
     */
    public byte[] encodeFrame(int frameId, Map<String, Double> signals) {
        if (signals == null || signals.isEmpty())
            return new byte[0];

        int count = signals.size();
        String[] names  = new String[count];
        double[] values = new double[count];

        int i = 0;
        for (Map.Entry<String, Double> e : signals.entrySet()) {
            names[i]  = e.getKey();
            values[i] = e.getValue();
            i++;
        }

        int maxSize = 64;
        Pointer outPtr = new Memory(maxSize);
        outPtr.clear(maxSize);

        int rc = lib.USDE_EncodeFrame(handle, frameId,
                names, values, count, outPtr, maxSize);

        if (rc != 1) return new byte[0];

        byte[] result = new byte[maxSize];
        outPtr.read(0, result, 0, maxSize);
        return result;
    }

    // ─── Lifecycle ─────────────────────────────────────────────────────

    /** Release native resources. Safe to call multiple times. */
    @Override
    public void close() throws IOException {
        if (handle != null) {
            lib.USDE_DestroyNetwork(handle);
            handle = null;
        }
    }
}
