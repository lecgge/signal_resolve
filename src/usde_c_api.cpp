#include "usde_c_api.h"
#include "usde_types.h"
#include "dbc_parser.h"
#include "ldf_parser.h"
#include "arxml_parser.h"
#include "codec_engine.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>

// ============================================================================
// Handle wrapper — owns a usde::NetworkCluster with a recursive_mutex for
// thread-safe mutation.  The mutex is recursive so that a single thread can
// call Load* multiple times without deadlocking.
// ============================================================================

struct NetworkContext {
    usde::NetworkCluster cluster;
    std::recursive_mutex mtx;
};

// ─── helpers ────────────────────────────────────────────────────────────────

static NetworkContext* Cast(USDE_NetworkHandle h) {
    return static_cast<NetworkContext*>(h);
}

// Copy a C++ string into a fixed-size char buffer (null-terminated, truncated).
static void CopyStr(char* dst, size_t cap, const std::string& src) {
    if (cap == 0) return;
    size_t n = src.size();
    if (n >= cap) n = cap - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

// ─── Lifecycle ──────────────────────────────────────────────────────────────

USDE_API USDE_NetworkHandle USDE_CreateNetwork(void) {
    try {
        auto* ctx = new NetworkContext();
        return static_cast<USDE_NetworkHandle>(ctx);
    } catch (...) {
        return nullptr;
    }
}

USDE_API int USDE_LoadDBC(USDE_NetworkHandle handle, const char* file_path) {
    if (!handle || !file_path) return 0;
    auto* ctx = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mtx);
    try {
        return usde::LoadDBC(file_path, ctx->cluster) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

USDE_API int USDE_LoadLDF(USDE_NetworkHandle handle, const char* file_path) {
    if (!handle || !file_path) return 0;
    auto* ctx = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mtx);
    try {
        return usde::LoadLDF(file_path, ctx->cluster) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

USDE_API int USDE_LoadARXML(USDE_NetworkHandle handle, const char* file_path) {
    if (!handle || !file_path) return 0;
    auto* ctx = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mtx);
    try {
        return usde::LoadARXML(file_path, ctx->cluster) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

USDE_API void USDE_DestroyNetwork(USDE_NetworkHandle handle) {
    if (!handle) return;
    delete Cast(handle);
}

// ─── Query ──────────────────────────────────────────────────────────────────

USDE_API int USDE_GetFrameCount(USDE_NetworkHandle handle) {
    if (!handle) return -1;
    auto* ctx = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mtx);
    return static_cast<int>(ctx->cluster.frames.size());
}

// ─── Decode (zero-allocation hot path) ──────────────────────────────────────

USDE_API int USDE_DecodeFrame(
    USDE_NetworkHandle   handle,
    uint32_t             frame_id,
    const uint8_t*       raw_bytes,
    int                  raw_size,
    C_DecodedSignal*     out_signals,
    int                  max_count,
    int*                 out_count)
{
    if (!handle || !raw_bytes || raw_size <= 0 ||
        !out_signals || max_count <= 0 || !out_count)
        return -1;

    auto* ctx = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mtx);

    auto it = ctx->cluster.frames.find(frame_id);
    if (it == ctx->cluster.frames.end()) {
        *out_count = 0;
        return 0;
    }

    // Use the core CodecEngine to decode — this returns a temporary vector,
    // which we then copy into the caller's fixed buffer.  The vector is
    // small (typically <50 signals) and lives on the stack via SBO in most
    // standard library implementations.
    auto decoded = usde::CodecEngine::DecodeFrame(
        it->second, raw_bytes, static_cast<size_t>(raw_size));

    int n = static_cast<int>(decoded.size());
    if (n > max_count) n = max_count;

    for (int i = 0; i < n; ++i) {
        CopyStr(out_signals[i].name, sizeof(out_signals[i].name),
                decoded[static_cast<size_t>(i)].name);
        out_signals[i].physical_value =
            decoded[static_cast<size_t>(i)].physical_value;
        CopyStr(out_signals[i].unit, sizeof(out_signals[i].unit),
                decoded[static_cast<size_t>(i)].unit);
    }
    *out_count = n;
    return 1;
}

// ─── Encode ─────────────────────────────────────────────────────────────────

USDE_API int USDE_EncodeFrame(
    USDE_NetworkHandle   handle,
    uint32_t             frame_id,
    const char* const*   signal_names,
    const double*        signal_values,
    int                  signal_count,
    uint8_t*             out_bytes,
    int                  max_size)
{
    if (!handle || !out_bytes || max_size <= 0)
        return -1;
    if (signal_count > 0 && (!signal_names || !signal_values))
        return -1;

    auto* ctx = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mtx);

    auto it = ctx->cluster.frames.find(frame_id);
    if (it == ctx->cluster.frames.end()) return 0;

    // Build the name->value map
    std::unordered_map<std::string, double> values;
    for (int i = 0; i < signal_count; ++i) {
        if (signal_names[i])
            values[signal_names[i]] = signal_values[i];
    }

    return usde::CodecEngine::EncodeFrame(
               it->second, values,
               out_bytes, static_cast<size_t>(max_size))
               ? 1
               : 0;
}
