package com.usde;

import com.sun.jna.Memory;
import com.sun.jna.Native;
import com.sun.jna.Pointer;

/**
 * Demonstration: load a DBC file via JNA, decode a frame, and print results.
 *
 * <p>Prerequisites:
 * <ul>
 *   <li>usde.dll / libusde.so must be on java.library.path</li>
 *   <li>jna.jar must be on the classpath</li>
 * </ul>
 *
 * <p>Compile &amp; run (Windows):
 * <pre>
 *   javac -cp jna.jar java/com/usde/*.java
 *   java  -cp jna.jar;java -Djava.library.path=build/Release com.usde.DemoUsde
 * </pre>
 */
public class DemoUsde {

    public static void main(String[] args) {
        UsdeLibrary lib = UsdeLibrary.INSTANCE;

        // 1. Create network
        Pointer net = lib.USDE_CreateNetwork();
        if (net == null) {
            System.err.println("Failed to create network.");
            return;
        }

        // 2. Load DBC
        String dbcPath = args.length > 0 ? args[0] : "test_data/main.dbc";
        int rc = lib.USDE_LoadDBC(net, dbcPath);
        System.out.println("LoadDBC(\"" + dbcPath + "\") = " + rc);
        System.out.println("Frames loaded: " + lib.USDE_GetFrameCount(net));

        if (rc == 0) {
            System.err.println("DBC load failed — aborting.");
            lib.USDE_DestroyNetwork(net);
            return;
        }

        // 3. Prepare a fake raw CAN frame (8 bytes)
        //    For demo purposes we'll use frame ID 0x345 (AMP_CFCAN_FrP01)
        //    which has 1 signal: AMPWorkSta at bit=15, len=1, Motorola
        int frameId = 0x345;
        byte[] raw = new byte[8];
        raw[0] = 0x00;
        raw[1] = (byte) 0x80; // bit 15 = 1

        Pointer rawPtr = new Memory(raw.length);
        rawPtr.write(0, raw, 0, raw.length);

        // 4. Decode
        int maxSignals = 64;
        UsdeLibrary.C_DecodedSignal[] outArr =
                (UsdeLibrary.C_DecodedSignal[]) new UsdeLibrary.C_DecodedSignal()
                        .toArray(maxSignals);

        int[] outCount = new int[1];
        int decRc = lib.USDE_DecodeFrame(net, frameId,
                rawPtr, raw.length,
                outArr, maxSignals, outCount);

        System.out.println("\nDecodeFrame(0x" + Integer.toHexString(frameId) + ") = " + decRc);
        System.out.println("Signals decoded: " + outCount[0]);

        for (int i = 0; i < outCount[0]; i++) {
            outArr[i].read();
            System.out.printf("  %s = %.4f %s%n",
                    outArr[i].getNameString(),
                    outArr[i].physicalValue,
                    outArr[i].getUnitString());
        }

        // 5. Cleanup
        lib.USDE_DestroyNetwork(net);
        System.out.println("\nNetwork destroyed. Done.");
    }
}
