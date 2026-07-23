#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace sigurdos::hal {

enum class OtaWriteResult : uint8_t {
    Accepted,
    InvalidArgument,
    EmptyChunk,
    ShortWrite,
    ExceedsExpected,
    CounterOverflow,
};

inline OtaWriteResult otaRecordExactWrite(size_t current, size_t requested,
                                          size_t written, size_t expected_total,
                                          size_t* next) {
    if (!next) return OtaWriteResult::InvalidArgument;
    *next = current;
    if (requested == 0) return OtaWriteResult::EmptyChunk;
    if (written != requested) return OtaWriteResult::ShortWrite;
    if (current > std::numeric_limits<size_t>::max() - written) {
        return OtaWriteResult::CounterOverflow;
    }
    const size_t candidate = current + written;
    if (expected_total != 0 && candidate > expected_total) {
        return OtaWriteResult::ExceedsExpected;
    }
    *next = candidate;
    return OtaWriteResult::Accepted;
}

inline bool otaWriteAccepted(OtaWriteResult result) {
    return result == OtaWriteResult::Accepted;
}

}  // namespace sigurdos::hal
