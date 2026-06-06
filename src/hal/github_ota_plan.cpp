#include "github_ota_plan.h"

#include <cstring>

namespace sigurdos {
namespace github_ota {
namespace {

const char* skipWs(const char* p) {
    while (p && *p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        ++p;
    }
    return p;
}

bool copyBounded(char* out, size_t out_size, const char* begin, size_t len) {
    if (!out || out_size == 0) return false;
    size_t copy_len = len < out_size - 1 ? len : out_size - 1;
    if (copy_len > 0) {
        std::memcpy(out, begin, copy_len);
    }
    out[copy_len] = '\0';
    return true;
}

bool readJsonString(const char*& p, char* out, size_t out_size) {
    p = skipWs(p);
    if (!p || *p != '"') return false;
    ++p;

    size_t i = 0;
    bool escaped = false;
    while (*p) {
        char c = *p++;
        if (escaped) {
            escaped = false;
            if (out && out_size > 0 && i < out_size - 1) {
                if (c == 'n') out[i++] = '\n';
                else if (c == 'r') out[i++] = '\r';
                else if (c == 't') out[i++] = '\t';
                else out[i++] = c;
            }
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            if (out && out_size > 0) out[i] = '\0';
            return true;
        }
        if (out && out_size > 0 && i < out_size - 1) {
            out[i++] = c;
        }
    }

    if (out && out_size > 0) out[0] = '\0';
    return false;
}

bool skipString(const char*& p) {
    char ignored[1];
    return readJsonString(p, ignored, sizeof(ignored));
}

bool skipJsonValue(const char*& p);

bool skipCompound(const char*& p, char open, char close) {
    p = skipWs(p);
    if (!p || *p != open) return false;
    ++p;
    while (*p) {
        p = skipWs(p);
        if (*p == close) {
            ++p;
            return true;
        }
        if (open == '{') {
            if (!skipString(p)) return false;
            p = skipWs(p);
            if (*p != ':') return false;
            ++p;
        }
        if (!skipJsonValue(p)) return false;
        p = skipWs(p);
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == close) {
            ++p;
            return true;
        }
        return false;
    }
    return false;
}

bool skipBareToken(const char*& p) {
    p = skipWs(p);
    if (!p || !*p) return false;
    const char* start = p;
    while (*p && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
        ++p;
    }
    return p != start;
}

bool readBool(const char*& p, bool* out) {
    p = skipWs(p);
    if (!p) return false;
    if (std::strncmp(p, "true", 4) == 0) {
        if (out) *out = true;
        p += 4;
        return true;
    }
    if (std::strncmp(p, "false", 5) == 0) {
        if (out) *out = false;
        p += 5;
        return true;
    }
    return false;
}

bool skipJsonValue(const char*& p) {
    p = skipWs(p);
    if (!p || !*p) return false;
    if (*p == '"') return skipString(p);
    if (*p == '{') return skipCompound(p, '{', '}');
    if (*p == '[') return skipCompound(p, '[', ']');
    return skipBareToken(p);
}

bool parseReleaseObject(const char*& p, char* tag, size_t tag_size,
                        char* target, size_t target_size,
                        bool* prerelease) {
    p = skipWs(p);
    if (!p || *p != '{') return false;
    ++p;

    bool saw_any = false;
    while (*p) {
        p = skipWs(p);
        if (*p == '}') {
            ++p;
            return saw_any;
        }

        char key[32] = "";
        if (!readJsonString(p, key, sizeof(key))) return false;
        p = skipWs(p);
        if (*p != ':') return false;
        ++p;

        if (std::strcmp(key, "tag_name") == 0) {
            if (!readJsonString(p, tag, tag_size)) return false;
        } else if (std::strcmp(key, "target_commitish") == 0) {
            if (!readJsonString(p, target, target_size)) return false;
        } else if (std::strcmp(key, "prerelease") == 0) {
            if (!readBool(p, prerelease)) return false;
        } else if (!skipJsonValue(p)) {
            return false;
        }

        saw_any = true;
        p = skipWs(p);
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == '}') {
            ++p;
            return true;
        }
        return false;
    }

    return false;
}

}  // namespace

bool branchNeedsReleaseApi(const char* branch) {
    return branch && branch[0] != '\0' && std::strcmp(branch, "latest") != 0;
}

bool selectReleaseTagFromJson(const char* json, const char* branch,
                              bool allow_prerelease,
                              char* tag_out, size_t tag_max) {
    if (tag_out && tag_max > 0) tag_out[0] = '\0';
    if (!json || !branchNeedsReleaseApi(branch) || !tag_out || tag_max == 0) {
        return false;
    }

    const char* p = skipWs(json);
    bool array = false;
    if (*p == '[') {
        array = true;
        ++p;
    }

    while (*p) {
        p = skipWs(p);
        if (array && *p == ']') return false;
        if (*p != '{') return false;

        char tag[96] = "";
        char target[32] = "";
        bool prerelease = false;
        if (!parseReleaseObject(p, tag, sizeof(tag), target, sizeof(target),
                                &prerelease)) {
            return false;
        }

        if (tag[0] && std::strcmp(target, branch) == 0 &&
            (allow_prerelease || !prerelease)) {
            return copyBounded(tag_out, tag_max, tag, std::strlen(tag));
        }

        p = skipWs(p);
        if (array) {
            if (*p == ',') {
                ++p;
                continue;
            }
            if (*p == ']') return false;
            return false;
        }
        return false;
    }

    return false;
}

void buildReleaseDownloadUrl(const char* tag, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (!tag || tag[0] == '\0') {
        out[0] = '\0';
        return;
    }

    static constexpr const char* kPrefix =
        "https://github.com/hermes-gadget/SigurdOS-tdeck/releases/download/";
    static constexpr const char* kSuffix = "/firmware.bin";

    size_t pos = 0;
    auto append = [&](const char* s) {
        while (*s && pos + 1 < out_size) {
            out[pos++] = *s++;
        }
    };
    append(kPrefix);
    append(tag);
    append(kSuffix);
    out[pos] = '\0';
}

void copyFallbackDownloadUrl(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    copyBounded(out, out_size, GITHUB_OTA_FALLBACK_URL,
                std::strlen(GITHUB_OTA_FALLBACK_URL));
}

}  // namespace github_ota
}  // namespace sigurdos
