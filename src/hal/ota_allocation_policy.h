// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace sigurdos {
namespace hal {

enum class OtaAllocationKind {
    WebServer,
    ApiSecureClient,
    ApiHttpClient,
    DownloadSecureClient,
    DownloadHttpClient,
};

struct OtaAllocationOps {
    void* context;
    void* (*create)(void*, OtaAllocationKind);
    void (*destroy)(void*, OtaAllocationKind, void*);
};

struct OtaHttpObjects {
    void* client;
    void* http;
};

inline bool allocate_ota_object(const OtaAllocationOps& ops,
                                OtaAllocationKind kind, void** object)
{
    if (!object) return false;
    *object = nullptr;
    if (!ops.create) return false;
    *object = ops.create(ops.context, kind);
    return *object != nullptr;
}

inline bool allocate_ota_http_objects(const OtaAllocationOps& ops,
                                      OtaAllocationKind client_kind,
                                      OtaAllocationKind http_kind,
                                      OtaHttpObjects* objects)
{
    if (!objects) return false;
    objects->client = nullptr;
    objects->http = nullptr;

    if (!allocate_ota_object(ops, client_kind, &objects->client)) return false;
    if (allocate_ota_object(ops, http_kind, &objects->http)) return true;

    if (ops.destroy) ops.destroy(ops.context, client_kind, objects->client);
    objects->client = nullptr;
    return false;
}

}  // namespace hal
}  // namespace sigurdos
