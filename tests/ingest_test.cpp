// Headless ingestion smoke test, exercised through the C bridge (same surface
// the SwiftUI app uses). Exits non-zero on failure. Run:
//   swift run IngestTest testdata/phantom

#include <cmath>
#include <cstdio>

#include "lumen_bridge.h"

static int fail(const char* msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <dicom_folder>\n", argv[0]);
        return 2;
    }

    char msg[512] = {0};
    LumenVolume* v = lumen_load_folder(argv[1], msg, sizeof(msg));
    std::printf("%s\n", msg);
    if (v == nullptr) return fail("load returned NULL");

    int w = 0, h = 0, d = 0;
    lumen_dims(v, &w, &h, &d);
    float sx = 0, sy = 0, sz = 0;
    lumen_spacing(v, &sx, &sy, &sz);
    float lo = 0, hi = 0;
    lumen_hu_range(v, &lo, &hi);
    std::printf("dims=%dx%dx%d spacing=%.2f/%.2f/%.2f HU[%.0f,%.0f]\n", w, h, d,
                sx, sy, sz, lo, hi);

    if (d < 2) return fail("expected a multi-slice volume");
    if (sx <= 0 || sy <= 0 || sz <= 0) return fail("non-positive spacing");
    if (lo > -500.0f) return fail("hu_min too high — rescale likely wrong");
    if (hi < 300.0f) return fail("hu_max too low — rescale likely wrong");

    // Center voxel of the middle slice should be soft tissue (~40 HU): proves
    // both the geometric Z-sort and the linear index mapping.
    const float center = lumen_sample_hu(v, w / 2, h / 2, d / 2);
    std::printf("center voxel HU = %.1f (expected ~40)\n", center);
    if (std::fabs(center - 40.0f) > 60.0f)
        return fail("center HU off — sort or indexing wrong");

    // Sanity-check a slice extraction round-trips to a sensible RGBA buffer.
    int sw = 0, sh = 0;
    const unsigned char* rgba =
        lumen_extract_slice(v, LUMEN_AXIS_AXIAL, d / 2, 40.0f, 400.0f, &sw, &sh);
    if (rgba == nullptr || sw != w || sh != h)
        return fail("axial slice extraction produced wrong dimensions");

    lumen_free(v);
    std::printf("PASS\n");
    return 0;
}
