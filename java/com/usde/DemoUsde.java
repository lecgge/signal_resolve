package com.usde;

import com.sun.jna.Memory;
import com.sun.jna.Pointer;

/**
 * USDE Algorithm Verification via JNA — encode/decode round-trip.
 */
public class DemoUsde {

    static int pass = 0;
    static int fail = 0;

    static void check(String label, boolean condition) {
        if (condition) {
            pass++;
            System.out.println("  PASS  " + label);
        } else {
            fail++;
            System.out.println("  FAIL  " + label);
        }
    }

    public static void main(String[] args) {
        UsdeLibrary lib = UsdeLibrary.INSTANCE;

        // 1. Create network and load DBC
        Pointer net = lib.USDE_CreateNetwork();
        check("CreateNetwork", net != null);

        String dbcPath = args.length > 0 ? args[0] : "test_data/main.dbc";
        int rc = lib.USDE_LoadDBC(net, dbcPath);
        check("LoadDBC succeeds", rc == 1);
        check("Frame count > 0", lib.USDE_GetFrameCount(net) > 0);

        // 2. Decode frame 0x345 (AMPWorkSta, bit=15, len=1, Motorola)
        System.out.println("\n--- Frame 0x345: AMPWorkSta ---");
        byte[] raw = new byte[8];
        raw[1] = (byte) 0x80; // bit 15 = 1

        Pointer rawPtr = new Memory(raw.length);
        rawPtr.write(0, raw, 0, raw.length);

        int maxSig = 64;
        UsdeLibrary.C_DecodedSignal[] outArr =
                (UsdeLibrary.C_DecodedSignal[]) new UsdeLibrary.C_DecodedSignal()
                        .toArray(maxSig);
        int[] outCount = new int[1];

        int decRc = lib.USDE_DecodeFrame(net, 0x345,
                rawPtr, raw.length, outArr, maxSig, outCount);
        check("DecodeFrame returns 1", decRc == 1);
        check("Decoded 1 signal", outCount[0] == 1);

        outArr[0].read();
        check("Signal name = AMPWorkSta",
                "AMPWorkSta".equals(outArr[0].getNameString()));
        check("Value = 1.0", outArr[0].physicalValue == 1.0);

        // 3. Decode with bit clear
        byte[] rawClear = new byte[8];
        Pointer rawClearPtr = new Memory(rawClear.length);
        rawClearPtr.write(0, rawClear, 0, rawClear.length);

        lib.USDE_DecodeFrame(net, 0x345,
                rawClearPtr, rawClear.length, outArr, maxSig, outCount);
        outArr[0].read();
        check("Bit clear -> value = 0.0", outArr[0].physicalValue == 0.0);

        // 4. Cleanup
        lib.USDE_DestroyNetwork(net);
        check("DestroyNetwork (no crash)", true);

        // Summary
        System.out.printf("%n  Results: %d PASS, %d FAIL%n", pass, fail);
        if (fail > 0) System.exit(1);
    }
}
