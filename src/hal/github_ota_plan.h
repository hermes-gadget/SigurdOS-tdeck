#pragma once

#include <cstddef>

namespace sigurdos {
namespace github_ota {

static constexpr size_t GITHUB_OTA_TRUST_ANCHOR_COUNT = 2;

constexpr const char* GITHUB_OTA_FALLBACK_URL =
    "https://github.com/hermes-gadget/SigurdOS-tdeck"
    "/releases/latest/download/firmware.bin";

enum class ReleaseSelectionResult {
    Matched,
    NoMatch,
    InvalidJson,
};

enum class ApiBodyStatus {
    Complete,
    Incomplete,
    TooLarge,
};

bool isSupportedReleaseChannel(const char* branch);
bool releaseChannelAllowsFallback(const char* branch);
bool branchNeedsReleaseApi(const char* branch, bool allow_prerelease);

ApiBodyStatus classifyApiResponseBody(int content_length, size_t bytes_read,
                                      size_t payload_capacity,
                                      bool stream_ended);

ReleaseSelectionResult selectReleaseTagResultFromJson(
    const char* json, const char* branch, bool allow_prerelease,
    char* tag_out, size_t tag_max,
    char* target_out = nullptr, size_t target_max = 0);

bool selectReleaseTagFromJson(const char* json, const char* branch,
                              bool allow_prerelease,
                              char* tag_out, size_t tag_max);

void buildReleaseDownloadUrl(const char* tag, char* out, size_t out_size);
void copyFallbackDownloadUrl(char* out, size_t out_size);

}  // namespace github_ota
}  // namespace sigurdos
