package com.usde;

import java.util.HashMap;
import java.util.Map;

/**
 * USDE Java demo — high-level API usage.
 *
 * <p>Run:
 * <pre>
 *   java -cp java/jna-5.14.0.jar;java/build
 *        -Djava.library.path=build/Release
 *        com.usde.DemoUsde
 * </pre>
 */
public class DemoUsde {

    public static void main(String[] args) {
        String dbcPath = args.length > 0 ? args[0] : "test_data/main.dbc";

        // Use try-with-resources for automatic cleanup
        try (UsdeClient client = new UsdeClient()) {

            // 1. Load DBC
            boolean ok = client.loadDbc(dbcPath);
            System.out.println("LoadDBC: " + ok + ", frames: " + client.getFrameCount());

            // 2. Prepare raw CAN bytes (frame 0x345: AMPWorkSta)
            byte[] raw = new byte[]{0x00, (byte) 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

            // 3. Decode — one call
            UsdeClient.DecodedSignal[] sigs = client.decodeFrame(0x345, raw);
            System.out.println("Decoded " + sigs.length + " signal(s):");
            for (UsdeClient.DecodedSignal s : sigs) {
                System.out.println("  " + s);
            }

            // 4. Encode — one call
            Map<String, Double> values = new HashMap<>();
            values.put("AMPWorkSta", 1.0);
            byte[] encoded = client.encodeFrame(0x345, values);
            System.out.print("Encoded AMPWorkSta=1: ");
            for (byte b : encoded) System.out.printf("%02X ", b);
            System.out.println();

            // 5. Round-trip verify
            UsdeClient.DecodedSignal[] rt = client.decodeFrame(0x345, encoded);
            System.out.println("Round-trip: " + rt[0]);

        } catch (Exception e) {
            System.err.println("Error: " + e.getMessage());
            e.printStackTrace();
        }
    }
}
