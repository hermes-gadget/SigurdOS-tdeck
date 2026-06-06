#pragma once

#include <cstddef>

namespace sigurdos {
namespace github_ota {

constexpr const char* GITHUB_OTA_FALLBACK_URL =
    "https://github.com/hermes-gadget/SigurdOS-tdeck"
    "/releases/latest/download/firmware.bin";

bool branchNeedsReleaseApi(const char* branch);

bool selectReleaseTagFromJson(const char* json, const char* branch,
                              bool allow_prerelease,
                              char* tag_out, size_t tag_max);

void buildReleaseDownloadUrl(const char* tag, char* out, size_t out_size);
void copyFallbackDownloadUrl(char* out, size_t out_size);

}  // namespace github_ota
}  // namespace sigurdos
