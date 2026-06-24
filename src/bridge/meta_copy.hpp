// Internal (non-API) helper for the two-call string-out pattern used by
// lumen_meta_json. Factored out of the bridge so the truncation / NUL-termination
// semantics can be unit-tested without constructing an opaque LumenVolume.
//
// Two-call contract: pass out=nullptr (or out_cap=0) to query the FULL length,
// then call again with a buffer of that length + 1. When the buffer is too
// small, at most out_cap-1 bytes are written and the result is NUL-terminated;
// the return value is always the full length (never the truncated length).

#pragma once

#include <algorithm>
#include <cstring>
#include <string>

namespace lumen {

inline int copy_string_out(const std::string& src, char* out, int out_cap) {
    const int full_len = static_cast<int>(src.size());
    if (out != nullptr && out_cap > 0) {
        const int copy_len = std::min(full_len, out_cap - 1);
        std::memcpy(out, src.data(), static_cast<size_t>(copy_len));
        out[copy_len] = '\0';
    }
    return full_len;
}

} // namespace lumen
