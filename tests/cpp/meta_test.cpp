// Unit test for lumen::serialize_meta_json - the producer half of the metadata
// round-trip the eng review flagged as a silent-corruption risk. Constructs
// metadata with special bytes (quote, backslash, tab, newline, a control char)
// and asserts the JSON escapes them and leaks no raw control characters. Pure
// C++, no DICOM file required. Run with `swift run MetaTest`.

#include <cstdio>
#include <string>
#include <vector>

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

    if (g_failures == 0) {
        std::printf("meta_test: all checks passed\n");
        return 0;
    }
    std::printf("meta_test: %d check(s) FAILED\n", g_failures);
    return 1;
}
