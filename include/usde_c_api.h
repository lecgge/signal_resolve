#ifndef USDE_C_API_H
#define USDE_C_API_H

#include <stdint.h>

#ifdef _WIN32
    #define USDE_API __declspec(dllexport)
#else
    #define USDE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ─── Opaque handle ──────────────────────────────────────────────────────────
typedef void* USDE_NetworkHandle;

// ─── C-compatible decoded signal (fixed-size, JNA-friendly) ─────────────────
typedef struct {
    char   name[128];
    double physical_value;
    char   unit[32];
} C_DecodedSignal;

// ─── Lifecycle ──────────────────────────────────────────────────────────────

// Create an empty network handle (for subsequent Load calls).
// Returns NULL on allocation failure.
USDE_API USDE_NetworkHandle USDE_CreateNetwork(void);

// Parse a DBC file and populate the network. Returns 1 on success, 0 on failure.
USDE_API int USDE_LoadDBC(USDE_NetworkHandle handle, const char* file_path);

// Parse a LDF file and populate the network. Returns 1 on success, 0 on failure.
USDE_API int USDE_LoadLDF(USDE_NetworkHandle handle, const char* file_path);

// Parse an ARXML file and populate the network. Returns 1 on success, 0 on failure.
USDE_API int USDE_LoadARXML(USDE_NetworkHandle handle, const char* file_path);

// Destroy the network and free all associated memory.
// Safe to call with NULL.  The handle is invalid after this call.
USDE_API void USDE_DestroyNetwork(USDE_NetworkHandle handle);

// ─── Query ──────────────────────────────────────────────────────────────────

// Get the number of frames in the network. Returns -1 if handle is invalid.
USDE_API int USDE_GetFrameCount(USDE_NetworkHandle handle);

// ─── Decode ─────────────────────────────────────────────────────────────────

// Decode a frame by its ID, writing results directly into the caller-provided
// |out_signals| buffer (zero-allocation hot path).
//
// |max_count|  : capacity of the out_signals array.
// |out_count|  : receives the actual number of signals written.
//
// Returns  1 on success (frame found and decoded).
// Returns  0 if the frame ID was not found.
// Returns -1 on invalid arguments.
USDE_API int USDE_DecodeFrame(
    USDE_NetworkHandle   handle,
    uint32_t             frame_id,
    const uint8_t*       raw_bytes,
    int                  raw_size,
    C_DecodedSignal*     out_signals,
    int                  max_count,
    int*                 out_count);

// ─── Encode ─────────────────────────────────────────────────────────────────

// Encode a frame by its ID, packing the given physical values into |out_bytes|.
//
// |signal_names|  : array of signal name strings (null-terminated).
// |signal_values| : array of physical values, parallel to signal_names.
// |signal_count|  : number of entries in the above arrays.
//
// Returns 1 on success, 0 if frame not found, -1 on invalid arguments.
USDE_API int USDE_EncodeFrame(
    USDE_NetworkHandle   handle,
    uint32_t             frame_id,
    const char* const*   signal_names,
    const double*        signal_values,
    int                  signal_count,
    uint8_t*             out_bytes,
    int                  max_size);

#ifdef __cplusplus
}
#endif

#endif // USDE_C_API_H
