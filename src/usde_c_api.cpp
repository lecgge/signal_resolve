#include "usde_c_api.h"
#include "usde_types.h"
#include "dbc_parser.h"
#include "ldf_parser.h"
#include "arxml_parser.h"
#include "codec_engine.h"

#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>

#include <vector>
// ============================================================================
// Handle wrapper — owns a usde::NetworkCluster with a recursive_mutex for
// thread-safe mutation.  The mutex is recursive so that a single thread can
// call Load* multiple times without deadlocking.
// ============================================================================

struct SignalLocation {
    uint32_t frame_id = 0;
    size_t signal_index = 0;
};

struct NetworkContext {
    usde::NetworkCluster cluster;
    std::vector<SignalLocation> signal_index;
    std::recursive_mutex mtx;

    void rebuildSignalIndex() {
        std::vector<uint32_t> frame_ids;
        frame_ids.reserve(cluster.frames.size());
        for (const auto& [frame_id, frame] : cluster.frames) {
            (void)frame;
            frame_ids.push_back(frame_id);
        }
        std::sort(frame_ids.begin(), frame_ids.end());

        signal_index.clear();
        for (const uint32_t frame_id : frame_ids) {
            const auto frame = cluster.frames.find(frame_id);
            if (frame == cluster.frames.end()) continue;
            for (size_t index = 0; index < frame->second.signals.size(); ++index) {
                signal_index.push_back(SignalLocation{frame_id, index});
            }
        }
    }
};

// ─── helpers ────────────────────────────────────────────────────────────────

static NetworkContext* Cast(USDE_NetworkHandle h) {
    return static_cast<NetworkContext*>(h);
}

static size_t Utf8PrefixLength(const std::string& value, size_t max_bytes) {
    if (value.size() <= max_bytes) return value.size();
    size_t length = max_bytes;
    while (length > 0 &&
           (static_cast<unsigned char>(value[length]) & 0xc0U) == 0x80U) {
        --length;
    }
    return length;
}

// Copy a UTF-8 string into a fixed-size buffer without splitting a code point.
static void CopyStr(char* dst, size_t cap, const std::string& src) {
    if (cap == 0) return;
    const size_t n = Utf8PrefixLength(src, cap - 1U);
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

static int StringLength(const std::string& value) {
    if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return -1;
    return static_cast<int>(value.size());
}

static int CopyMetadataString(
    const std::string& value,
    char* destination,
    int capacity,
    int* required_capacity) {
    if (!required_capacity || capacity < 0) return -1;
    const int length = StringLength(value);
    if (length < 0 || length == std::numeric_limits<int>::max()) return -1;

    *required_capacity = length + 1;
    if (!destination || capacity < *required_capacity) {
        if (destination && capacity > 0) destination[0] = '\0';
        return 0;
    }
    std::memcpy(destination, value.data(), static_cast<size_t>(length));
    destination[length] = '\0';
    return 1;
}

static const usde::Signal* FindSignal(
    const NetworkContext* context,
    int signal_index,
    const usde::Frame** out_frame = nullptr) {
    if (!context || signal_index < 0 ||
        static_cast<size_t>(signal_index) >= context->signal_index.size()) {
        return nullptr;
    }
    const auto& location = context->signal_index[static_cast<size_t>(signal_index)];
    const auto frame = context->cluster.frames.find(location.frame_id);
    if (frame == context->cluster.frames.end() ||
        location.signal_index >= frame->second.signals.size()) {
        return nullptr;
    }
    if (out_frame) *out_frame = &frame->second;
    return &frame->second.signals[location.signal_index];
}

static const usde::ValueState* FindValueState(
    const NetworkContext* context,
    int signal_index,
    int state_index,
    const usde::Signal** out_signal = nullptr) {
    if (state_index < 0) return nullptr;
    const auto* signal = FindSignal(context, signal_index);
    if (!signal || static_cast<size_t>(state_index) >= signal->value_states.size()) {
        return nullptr;
    }
    if (out_signal) *out_signal = signal;
    return &signal->value_states[static_cast<size_t>(state_index)];
}

static double ToPhysicalValue(const usde::Signal& signal, const usde::ValueState& state) {
    char* end = nullptr;
    const long double raw = std::strtold(state.raw_value_text.c_str(), &end);
    if (!end || *end != '\0') return 0.0;
    return static_cast<double>(
        raw * static_cast<long double>(signal.factor) +
        static_cast<long double>(signal.offset));
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

USDE_API USDE_NetworkHandle USDE_CloneNetwork(USDE_NetworkHandle handle) {
    if (!handle) return nullptr;
    auto* source = Cast(handle);
    try {
        std::lock_guard<std::recursive_mutex> lock(source->mtx);
        auto* clone = new NetworkContext();
        clone->cluster = source->cluster;
        clone->signal_index = source->signal_index;
        return static_cast<USDE_NetworkHandle>(clone);
    } catch (...) {
        return nullptr;
    }
}


USDE_API int USDE_LoadDBC(USDE_NetworkHandle handle, const char* file_path) {
    if (!handle || !file_path) return 0;
    auto* ctx = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mtx);
    try {
        if (!usde::LoadDBC(file_path, ctx->cluster)) return 0;
        ctx->rebuildSignalIndex();
        return 1;
    } catch (...) {
        return 0;
    }
}

USDE_API int USDE_LoadLDF(USDE_NetworkHandle handle, const char* file_path) {
    if (!handle || !file_path) return 0;
    auto* ctx = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mtx);
    try {
        if (!usde::LoadLDF(file_path, ctx->cluster)) return 0;
        ctx->rebuildSignalIndex();
        return 1;
    } catch (...) {
        return 0;
    }
}

USDE_API int USDE_LoadARXML(USDE_NetworkHandle handle, const char* file_path) {
    if (!handle || !file_path) return 0;
    auto* ctx = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(ctx->mtx);
    try {
        if (!usde::LoadARXML(file_path, ctx->cluster)) return 0;
        ctx->rebuildSignalIndex();
        return 1;
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

USDE_API int USDE_GetMetadataCounts(
    USDE_NetworkHandle handle,
    C_MetadataCounts* out_counts) {
    if (!handle || !out_counts) return -1;
    auto* context = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(context->mtx);
    if (context->cluster.nodes.size() >
            static_cast<size_t>(std::numeric_limits<int>::max()) ||
        context->signal_index.size() >
            static_cast<size_t>(std::numeric_limits<int>::max())) {
        return -1;
    }
    out_counts->node_count = static_cast<int>(context->cluster.nodes.size());
    out_counts->signal_count = static_cast<int>(context->signal_index.size());
    return 1;
}

USDE_API int USDE_CopyMetadataNode(
    USDE_NetworkHandle handle,
    int node_index,
    char* destination,
    int capacity,
    int* required_capacity) {
    if (!handle || !required_capacity || capacity < 0) return -1;
    auto* context = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(context->mtx);
    if (node_index < 0 ||
        static_cast<size_t>(node_index) >= context->cluster.nodes.size()) {
        return 0;
    }
    return CopyMetadataString(
        context->cluster.nodes[static_cast<size_t>(node_index)],
        destination,
        capacity,
        required_capacity);
}

USDE_API int USDE_GetSignalMetadata(
    USDE_NetworkHandle handle,
    int signal_index,
    C_SignalMetadata* out_metadata) {
    if (!handle || !out_metadata) return -1;
    auto* context = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(context->mtx);

    const usde::Frame* frame = nullptr;
    const usde::Signal* signal = FindSignal(context, signal_index, &frame);
    if (!signal || !frame) return 0;
    if (signal->value_states.size() >
        static_cast<size_t>(std::numeric_limits<int>::max())) {
        return -1;
    }

    C_SignalMetadata metadata{};
    metadata.message_id = frame->id;
    metadata.signal_id = usde::CodecEngine::SignalId(signal->name);
    metadata.start_bit = signal->start_bit;
    metadata.bit_length = signal->bit_length;
    metadata.byte_order = static_cast<uint32_t>(signal->byte_order);
    metadata.factor = signal->factor;
    metadata.offset = signal->offset;
    metadata.min_value = signal->min_value;
    metadata.max_value = signal->max_value;
    metadata.value_state_count = static_cast<int>(signal->value_states.size());
    metadata.sender_node_length = StringLength(frame->transmitter);
    metadata.message_name_length = StringLength(frame->name);
    metadata.signal_name_length = StringLength(signal->name);
    metadata.unit_length = StringLength(signal->unit);
    if (metadata.sender_node_length < 0 ||
        metadata.message_name_length < 0 ||
        metadata.signal_name_length < 0 ||
        metadata.unit_length < 0) {
        return -1;
    }
    *out_metadata = metadata;
    return 1;
}

USDE_API int USDE_GetSignalMetadataV2(
    USDE_NetworkHandle handle,
    int signal_index,
    C_SignalMetadataV2* out_metadata) {
    if (!handle || !out_metadata) return -1;
    auto* context = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(context->mtx);
    const usde::Signal* signal = FindSignal(context, signal_index);
    if (!signal) return 0;

    C_SignalMetadataV2 metadata{};
    metadata.signal_id = usde::CodecEngine::SignalId64(signal->name);
    metadata.description_length = StringLength(signal->description);
    if (metadata.description_length < 0) return -1;
    if (signal->is_mux_decoder) metadata.flags |= USDE_SIGNAL_FLAG_MUX_SELECTOR;
    if (signal->is_multiplexed) metadata.flags |= USDE_SIGNAL_FLAG_MULTIPLEXED;
    if (signal->is_signed) metadata.flags |= USDE_SIGNAL_FLAG_SIGNED;
    *out_metadata = metadata;
    return 1;
}

USDE_API int USDE_CopySignalMetadataString(
    USDE_NetworkHandle handle,
    int signal_index,
    int field,
    char* destination,
    int capacity,
    int* required_capacity) {
    if (!handle || !required_capacity || capacity < 0) return -1;
    auto* context = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(context->mtx);

    const usde::Frame* frame = nullptr;
    const usde::Signal* signal = FindSignal(context, signal_index, &frame);
    if (!signal || !frame) return 0;

    const std::string* value = nullptr;
    switch (field) {
        case USDE_METADATA_SIGNAL_SENDER_NODE: value = &frame->transmitter; break;
        case USDE_METADATA_SIGNAL_MESSAGE_NAME: value = &frame->name; break;
        case USDE_METADATA_SIGNAL_SIGNAL_NAME: value = &signal->name; break;
        case USDE_METADATA_SIGNAL_UNIT: value = &signal->unit; break;
        case USDE_METADATA_SIGNAL_DESCRIPTION: value = &signal->description; break;
        default: return -1;
    }
    return CopyMetadataString(*value, destination, capacity, required_capacity);
}

USDE_API int USDE_GetValueStateMetadata(
    USDE_NetworkHandle handle,
    int signal_index,
    int state_index,
    C_ValueStateMetadata* out_metadata) {
    if (!handle || !out_metadata) return -1;
    auto* context = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(context->mtx);

    const usde::Signal* signal = nullptr;
    const usde::ValueState* state =
        FindValueState(context, signal_index, state_index, &signal);
    if (!state || !signal) return 0;

    C_ValueStateMetadata metadata{};
    metadata.physical_value = ToPhysicalValue(*signal, *state);
    metadata.sort_order = state_index;
    metadata.raw_value_text_length = StringLength(state->raw_value_text);
    metadata.state_label_length = StringLength(state->label);
    if (metadata.raw_value_text_length < 0 || metadata.state_label_length < 0) {
        return -1;
    }
    *out_metadata = metadata;
    return 1;
}

USDE_API int USDE_CopyValueStateMetadataString(
    USDE_NetworkHandle handle,
    int signal_index,
    int state_index,
    int field,
    char* destination,
    int capacity,
    int* required_capacity) {
    if (!handle || !required_capacity || capacity < 0) return -1;
    auto* context = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(context->mtx);

    const usde::ValueState* state = FindValueState(context, signal_index, state_index);
    if (!state) return 0;
    switch (field) {
        case USDE_METADATA_VALUE_RAW_TEXT:
            return CopyMetadataString(
                state->raw_value_text, destination, capacity, required_capacity);
        case USDE_METADATA_VALUE_STATE_LABEL:
            return CopyMetadataString(
                state->label, destination, capacity, required_capacity);
        default:
            return -1;
    }
}

// ─── Decode (zero-allocation hot path) ──────────────────────────────────────

static int DecodeFrameValues(
    USDE_NetworkHandle handle,
    uint32_t frame_id,
    const uint8_t* raw_bytes,
    int raw_size,
    int max_count,
    int* out_count,
    std::vector<usde::DecodedSignal>& decoded) {
    if (!handle || !raw_bytes || raw_size <= 0 || max_count <= 0 || !out_count) {
        return -1;
    }

    auto* context = Cast(handle);
    std::lock_guard<std::recursive_mutex> lock(context->mtx);
    const auto frame = context->cluster.frames.find(frame_id);
    if (frame == context->cluster.frames.end()) {
        *out_count = 0;
        return 0;
    }

    decoded = usde::CodecEngine::DecodeFrame(
        frame->second, raw_bytes, static_cast<size_t>(raw_size));
    const int count = static_cast<int>(decoded.size());
    if (count > max_count) {
        *out_count = count;
        return USDE_DECODE_BUFFER_TOO_SMALL;
    }
    *out_count = count;
    return 1;
}

static void CopyDecodedSignal(
    C_DecodedSignal& destination,
    const usde::DecodedSignal& source) {
    CopyStr(destination.name, sizeof(destination.name), source.name);
    destination.physical_value = source.physical_value;
    CopyStr(destination.unit, sizeof(destination.unit), source.unit);
    destination.start_bit = source.start_bit;
    destination.bit_length = source.bit_length;
    destination.byte_order = static_cast<uint32_t>(source.byte_order);
    destination.factor = source.factor;
    destination.offset = source.offset;
    destination.min_value = source.min_value;
    destination.max_value = source.max_value;
    destination.message_id = source.message_id;
    destination.signal_id = source.signal_id;
}

USDE_API int USDE_DecodeFrame(
    USDE_NetworkHandle   handle,
    uint32_t             frame_id,
    const uint8_t*       raw_bytes,
    int                  raw_size,
    C_DecodedSignal*     out_signals,
    int                  max_count,
    int*                 out_count)
{
    if (!out_signals) return -1;
    std::vector<usde::DecodedSignal> decoded;
    const int result = DecodeFrameValues(
        handle, frame_id, raw_bytes, raw_size, max_count, out_count, decoded);
    if (result != 1) return result;
    for (size_t index = 0; index < decoded.size(); ++index) {
        CopyDecodedSignal(out_signals[index], decoded[index]);
    }
    return 1;
}

USDE_API int USDE_DecodeFrameV2(
    USDE_NetworkHandle handle,
    uint32_t frame_id,
    const uint8_t* raw_bytes,
    int raw_size,
    C_DecodedSignalV2* out_signals,
    int max_count,
    int* out_count) {
    if (!out_signals) return -1;
    std::vector<usde::DecodedSignal> decoded;
    const int result = DecodeFrameValues(
        handle, frame_id, raw_bytes, raw_size, max_count, out_count, decoded);
    if (result != 1) return result;
    for (size_t index = 0; index < decoded.size(); ++index) {
        CopyDecodedSignal(out_signals[index].signal, decoded[index]);
        out_signals[index].signal_id64 = decoded[index].signal_id64;
    }
    return 1;
}

USDE_API int USDE_DecodeFrameRaw(
    USDE_NetworkHandle handle,
    uint32_t frame_id,
    const uint8_t* raw_bytes,
    int raw_size,
    C_RawDecodedSignal* out_signals,
    int max_count,
    int* out_count) {
    if (!out_signals) return -1;
    std::vector<usde::DecodedSignal> decoded;
    const int result = DecodeFrameValues(
        handle, frame_id, raw_bytes, raw_size, max_count, out_count, decoded);
    if (result != 1) return result;
    for (size_t index = 0; index < decoded.size(); ++index) {
        out_signals[index].signal_id = decoded[index].signal_id64;
        out_signals[index].raw_bits = decoded[index].raw_bits;
    }
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
