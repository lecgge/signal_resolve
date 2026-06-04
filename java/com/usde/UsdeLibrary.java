package com.usde;

import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.Structure;

import java.util.Arrays;
import java.util.List;

/**
 * JNA interface for the USDE (Universal Signal Decoding Engine) native library.
 *
 * <p>Usage:
 * <pre>{@code
 *   UsdeLibrary lib = UsdeLibrary.INSTANCE;
 *   Pointer net = lib.USDE_CreateNetwork();
 *   lib.USDE_LoadDBC(net, "path/to/file.dbc");
 *   // ... decode / encode ...
 *   lib.USDE_DestroyNetwork(net);
 * }</pre>
 */
public interface UsdeLibrary extends Library {

    UsdeLibrary INSTANCE = Native.load("usde", UsdeLibrary.class);

    // ─── C-compatible decoded signal structure ──────────────────────────

    /**
     * Maps to the C {@code C_DecodedSignal} struct.
     * Field order MUST match the C layout exactly.
     */
    @Structure.FieldOrder({"name", "physicalValue", "unit"})
    class C_DecodedSignal extends Structure {
        /** char[128] */
        public byte[] name = new byte[128];
        public double physicalValue;
        /** char[32] */
        public byte[] unit = new byte[32];

        public String getNameString()  { return toNullTerminated(name); }
        public String getUnitString()  { return toNullTerminated(unit); }

        private static String toNullTerminated(byte[] buf) {
            int len = 0;
            while (len < buf.length && buf[len] != 0) len++;
            return new String(buf, 0, len);
        }
    }

    // ─── Lifecycle ──────────────────────────────────────────────────────

    Pointer USDE_CreateNetwork();

    int USDE_LoadDBC(Pointer handle, String filePath);
    int USDE_LoadLDF(Pointer handle, String filePath);
    int USDE_LoadARXML(Pointer handle, String filePath);

    void USDE_DestroyNetwork(Pointer handle);

    // ─── Query ──────────────────────────────────────────────────────────

    int USDE_GetFrameCount(Pointer handle);

    // ─── Decode ─────────────────────────────────────────────────────────

    /**
     * Decode a frame by ID.
     *
     * @param handle       network handle
     * @param frameId      CAN / LIN frame ID
     * @param rawBytes     pointer to raw byte buffer
     * @param rawSize      number of bytes
     * @param outSignals   pre-allocated array of C_DecodedSignal
     * @param maxCount     capacity of outSignals
     * @param outCount     one-element array receiving the actual count
     * @return 1 = success, 0 = frame not found, -1 = bad args
     */
    int USDE_DecodeFrame(Pointer handle, int frameId,
                         Pointer rawBytes, int rawSize,
                         C_DecodedSignal[] outSignals, int maxCount,
                         int[] outCount);

    // ─── Encode ─────────────────────────────────────────────────────────

    /**
     * Encode a frame by ID.
     *
     * @param handle        network handle
     * @param frameId       CAN / LIN frame ID
     * @param signalNames   array of null-terminated name pointers
     * @param signalValues  parallel array of physical values
     * @param signalCount   number of entries
     * @param outBytes      output buffer
     * @param maxSize       capacity of outBytes
     * @return 1 = success, 0 = frame not found, -1 = bad args
     */
    int USDE_EncodeFrame(Pointer handle, int frameId,
                         Pointer[] signalNames, double[] signalValues,
                         int signalCount,
                         Pointer outBytes, int maxSize);
}
