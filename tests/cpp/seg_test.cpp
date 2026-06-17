// Headless unit tests for the segmentation core: the plane_map round-trip (the
// one silent-failure path), threshold fill, region-grow connectivity + tolerance,
// the paint/erase disk, and the mask overlay. No DICOM file, no GPU — build and
// run with `swift run SegTest`.

#include <cstdio>
#include <memory>

#include "core/volume.h"
#include "geometry/plane_map.hpp"
#include "segmentation/label_volume.hpp"
#include "segmentation/mask_view.hpp"
#include "segmentation/segment.hpp"

using namespace lumen;

static int g_failures = 0;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("  FAIL: %s\n", (msg));                                 \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

// Build an N^3 volume with every voxel at `fill` HU.
static Volume make_volume(int n, float fill) {
    Volume v;
    v.width = v.height = v.depth = n;
    v.spacing_x = v.spacing_y = v.spacing_z = 1.0f;
    v.voxel_buffer = std::make_unique<float[]>(v.voxel_count());
    for (std::size_t i = 0; i < v.voxel_count(); ++i) v.voxel_buffer[i] = fill;
    v.hu_min = fill;
    v.hu_max = fill;
    return v;
}

static void set_hu(Volume& v, int x, int y, int z, float hu) {
    v.voxel_buffer[v.index(x, y, z)] = hu;
}

// 1. plane_map round-trip: pixel -> voxel -> pixel is identity on every axis,
//    including the coronal/sagittal vertical flip. This is the critical test.
static void test_plane_map_roundtrip() {
    std::printf("plane_map round-trip\n");
    Volume v = make_volume(6, 0.0f);
    const Axis axes[] = {Axis::Axial, Axis::Coronal, Axis::Sagittal};
    for (Axis axis : axes) {
        const SliceDims d = slice_dims(v, axis);
        CHECK(d.width > 0 && d.height > 0, "slice dims positive");
        const int index = 2;
        for (int py = 0; py < d.height; ++py) {
            for (int px = 0; px < d.width; ++px) {
                const VoxelCoord c = plane_to_voxel(v, axis, index, px, py);
                CHECK(c.x >= 0 && c.x < v.width && c.y >= 0 && c.y < v.height &&
                          c.z >= 0 && c.z < v.depth,
                      "mapped voxel in bounds");
                const PixelCoord p = voxel_to_plane(v, axis, c.x, c.y, c.z);
                CHECK(p.px == px && p.py == py, "round-trip identity");
            }
        }
    }
}

// 2. threshold_fill labels exactly the voxels whose HU is in range.
static void test_threshold() {
    std::printf("threshold_fill\n");
    Volume v = make_volume(8, -1000.0f);
    // Two 2x2x2 boxes at 300 HU.
    for (int z = 1; z <= 2; ++z)
        for (int y = 1; y <= 2; ++y)
            for (int x = 1; x <= 2; ++x) set_hu(v, x, y, z, 300.0f);
    for (int z = 5; z <= 6; ++z)
        for (int y = 5; y <= 6; ++y)
            for (int x = 5; x <= 6; ++x) set_hu(v, x, y, z, 300.0f);

    LabelVolume mask;
    mask.reset_to(v);
    threshold_fill(v, 200.0f, 400.0f, mask);
    CHECK(mask.count_nonzero() == 16, "threshold labels both boxes (16 voxels)");

    threshold_fill(v, 5000.0f, 6000.0f, mask); // nothing in range -> clears
    CHECK(mask.count_nonzero() == 0, "out-of-range threshold clears mask");
}

// 3. region_grow selects only the seeded connected component, respecting the gap.
static void test_region_grow() {
    std::printf("region_grow connectivity\n");
    Volume v = make_volume(8, -1000.0f);
    for (int z = 1; z <= 2; ++z)
        for (int y = 1; y <= 2; ++y)
            for (int x = 1; x <= 2; ++x) set_hu(v, x, y, z, 300.0f);
    for (int z = 5; z <= 6; ++z)
        for (int y = 5; y <= 6; ++y)
            for (int x = 5; x <= 6; ++x) set_hu(v, x, y, z, 300.0f);

    LabelVolume mask;
    mask.reset_to(v);
    const long added = region_grow(v, 1, 1, 1, 50.0f, mask);
    CHECK(added == 8, "grow selects only the seeded 2x2x2 box");
    CHECK(mask.count_nonzero() == 8, "second box not reached across the gap");
    CHECK(mask.at(5, 5, 5) == 0, "far box stays background");

    // Out-of-range seed is a no-op.
    LabelVolume mask2;
    mask2.reset_to(v);
    CHECK(region_grow(v, -1, 0, 0, 50.0f, mask2) == 0, "bad seed no-ops");
}

// 4. paint_disk paints, then erase undoes it; honours bounds.
static void test_paint() {
    std::printf("paint_disk\n");
    Volume v = make_volume(8, 0.0f);
    LabelVolume mask;
    mask.reset_to(v);

    // Disk radius 1 at axial (z=2), center (3,3): center + 4 edge pixels = 5.
    const long painted = paint_disk(v, Axis::Axial, 2, 3, 3, 1, true, mask);
    CHECK(painted == 5, "radius-1 disk paints 5 voxels");
    CHECK(mask.at(3, 3, 2) == kActiveLabel, "center voxel set on the right slice");
    CHECK(mask.at(3, 3, 1) == 0, "neighbouring slice untouched");

    const long erased = paint_disk(v, Axis::Axial, 2, 3, 3, 1, false, mask);
    CHECK(erased == 5, "erase clears the same 5 voxels");
    CHECK(mask.count_nonzero() == 0, "mask empty after erase");
}

// 5. ExtractMaskSlice paints the overlay where the mask is set, transparent else.
static void test_mask_overlay() {
    std::printf("ExtractMaskSlice overlay\n");
    Volume v = make_volume(8, 0.0f);
    LabelVolume mask;
    mask.reset_to(v);
    mask.set(3, 3, 2, kActiveLabel);

    SliceImage out;
    ExtractMaskSlice(v, mask, Axis::Axial, 2, out);
    CHECK(out.width == 8 && out.height == 8, "overlay matches slice dims");
    const std::size_t at = (static_cast<std::size_t>(3) * out.width + 3) * 4;
    CHECK(out.rgba[at + 3] > 0, "labelled pixel is opaque");
    const std::size_t off = (static_cast<std::size_t>(0) * out.width + 0) * 4;
    CHECK(out.rgba[off + 3] == 0, "unlabelled pixel is transparent");
}

int main() {
    std::printf("== SegTest ==\n");
    test_plane_map_roundtrip();
    test_threshold();
    test_region_grow();
    test_paint();
    test_mask_overlay();
    if (g_failures == 0) {
        std::printf("All segmentation tests passed.\n");
        return 0;
    }
    std::printf("%d assertion(s) failed.\n", g_failures);
    return 1;
}
