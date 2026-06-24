// Unit test for lumen::serialize_meta_json - the producer half of the metadata
// round-trip the eng review flagged as a silent-corruption risk. Constructs
// metadata with special bytes (quote, backslash, tab, newline, a control char)
// and asserts the JSON escapes them and leaks no raw control characters. Pure
// C++, no DICOM file required. Run with `swift run MetaTest`.

#include <cstdio>
#include <string>
#include <vector>

#include <cstring>

#include "bridge/meta_copy.hpp"
#include "io/dicom_meta.hpp"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
    lumen::StudyMeta meta;
    meta.patient_name = "DOE^JANE";
    meta.study_description = std::string("A \"quote\", slash \\, tab\t, nl\n, ctrl\x01");

    std::vector<lumen::DicomTag> tags;
    tags.push_back({"(0010,0010)", "PN", "PatientName", "DOE^JANE"});

    const std::string json = lumen::serialize_meta_json(meta, tags);

    check(contains(json, "\\\""), "escapes double-quote");
    check(contains(json, "\\\\"), "escapes backslash");
    check(contains(json, "\\t"), "escapes tab");
    check(contains(json, "\\n"), "escapes newline");
    check(contains(json, "\\u0001"), "escapes control char as \\u00xx");
    check(contains(json, "\"patient_name\":\"DOE^JANE\""), "emits curated field");
    check(contains(json, "\"(0010,0010)\""), "emits tag group/element");

    // No raw control characters may survive into the serialized output.
    check(json.find('\t') == std::string::npos, "no raw tab leaks through");
    check(json.find('\n') == std::string::npos, "no raw newline leaks through");
    check(json.find('\x01') == std::string::npos, "no raw control byte leaks through");

    // --- lumen::copy_string_out — the two-call / truncation contract that backs
    //     lumen_meta_json (out=nullptr queries length; small buffer truncates). ---
    const std::string blob = "hello world";          // 11 bytes
    const int full = static_cast<int>(blob.size());

    // (1) Length query (out=nullptr, out_cap=0) returns the full length.
    check(lumen::copy_string_out(blob, nullptr, 0) == full,
          "copy_string_out: null buffer returns full length");

    // (2) A right-sized buffer copies everything and NUL-terminates.
    {
        char buf[32];
        std::memset(buf, 'X', sizeof(buf));
        const int n = lumen::copy_string_out(blob, buf, sizeof(buf));
        check(n == full, "copy_string_out: full-size copy returns full length");
        check(buf[full] == '\0', "copy_string_out: full-size copy is NUL-terminated");
        check(std::string(buf) == blob, "copy_string_out: full-size copy round-trips");
    }

    // (3) Truncation: a small buffer still returns the FULL length but writes
    //     only out_cap-1 data bytes plus a NUL at out_cap-1.
    {
        const int cap = 5;                            // room for 4 chars + NUL
        char buf[cap];
        std::memset(buf, 'X', sizeof(buf));
        const int n = lumen::copy_string_out(blob, buf, cap);
        check(n == full, "copy_string_out: truncated copy still returns full length");
        check(buf[cap - 1] == '\0', "copy_string_out: truncated copy is NUL-terminated");
        check(std::string(buf) == blob.substr(0, cap - 1),
              "copy_string_out: truncated copy writes exactly out_cap-1 bytes");
    }

    // (4) out_cap == 1 writes only the NUL (no data bytes).
    {
        char buf[1] = {'X'};
        const int n = lumen::copy_string_out(blob, buf, 1);
        check(n == full, "copy_string_out: out_cap=1 returns full length");
        check(buf[0] == '\0', "copy_string_out: out_cap=1 writes only the NUL");
    }

    if (g_failures == 0) {
        std::printf("meta_test: all checks passed\n");
        return 0;
    }
    std::printf("meta_test: %d check(s) FAILED\n", g_failures);
    return 1;
}
