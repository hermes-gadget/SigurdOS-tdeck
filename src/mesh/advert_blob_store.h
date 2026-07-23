#pragma once

#include "advert_blob.h"
#include "hal/atomic_file.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos {
namespace mesh {

struct AdvertBlobWriteContext {
    const uint8_t* data;
    size_t length;
};

inline bool writeAdvertBlob(sigurdos::storage::AtomicFileWriter& writer,
                            void* raw)
{
    const auto* context = static_cast<const AdvertBlobWriteContext*>(raw);
    return context && context->data && advertBlobLengthValid(context->length) &&
           writer.write(context->data, context->length) == context->length &&
           writer.bytesWritten() == context->length;
}

inline bool validateAdvertBlob(sigurdos::storage::AtomicFileReader& reader,
                               void* raw)
{
    const size_t length = reader.size();
    if (!advertBlobLengthValid(length) || !reader.seek(0)) return false;

    uint8_t stored[SIGURDOS_ADVERT_BLOB_MAX_LEN];
    if (reader.read(stored, length) != length) return false;

    const auto* expected = static_cast<const AdvertBlobWriteContext*>(raw);
    return !expected ||
        (expected->data && expected->length == length &&
         std::memcmp(stored, expected->data, length) == 0);
}

} // namespace mesh
} // namespace sigurdos
