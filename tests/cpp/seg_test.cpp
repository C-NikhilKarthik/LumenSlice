// Headless unit tests for the segmentation core: the plane_map round-trip (the
// one silent-failure path), threshold fill, region-grow connectivity + tolerance,
// the paint/erase disk, and the mask overlay. No DICOM file, no GPU — build and
// run with `swift run SegTest`.

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <utility>

#include "core/volume.h"
#include "geometry/plane_map.hpp"
#include "segmentation/analysis.hpp"
#include "segmentation/effects.hpp"
#include "segmentation/label_volume.hpp"
#include "segmentation/marching_cubes.hpp"
#include "segmentation/mask_view.hpp"
#include "segmentation/segment.hpp"
#include "segmentation/segment_editor.hpp"
#include "segmentation/segment_table.hpp"
#include "segmentation/stl_export.hpp"
#include "segmentation/undo_stack.hpp"

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

    SegmentTable table;
    const std::uint8_t id = table.add(Rgb{0, 200, 255});
    CHECK(id == kActiveLabel, "first segment gets id 1 (kActiveLabel)");

    SliceImage out;
    ExtractMaskSlice(v, mask, table, Axis::Axial, 2, out);
    CHECK(out.width == 8 && out.height == 8, "overlay matches slice dims");
    const std::size_t at = (static_cast<std::size_t>(3) * out.width + 3) * 4;
    CHECK(out.rgba[at + 3] > 0, "labelled pixel is opaque");
    const std::size_t off = (static_cast<std::size_t>(0) * out.width + 0) * 4;
    CHECK(out.rgba[off + 3] == 0, "unlabelled pixel is transparent");
}

// 6. marching cubes on a solid box -> a closed manifold with outward normals.
static void test_marching_cubes() {
    std::printf("marching_cubes manifold\n");
    Volume v = make_volume(12, 0.0f);
    LabelVolume mask;
    mask.reset_to(v);
    // A solid box well inside the volume (not touching any face).
    for (int z = 3; z <= 8; ++z)
        for (int y = 3; y <= 8; ++y)
            for (int x = 3; x <= 8; ++x) mask.set(x, y, z, kActiveLabel);

    Mesh mesh;
    const int tris = marching_cubes(mask.data(), 12, 12, 12, 1, 1, 1, 0, 1, mesh);
    CHECK(tris > 0, "box produces triangles");
    CHECK(mesh.vertex_count() > 0, "box produces vertices");
    CHECK(mesh.triangle_count() == tris, "returned count matches buffer");

    // Closed manifold: every undirected edge is shared by exactly two triangles.
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t t[3] = {mesh.indices[i], mesh.indices[i + 1],
                                    mesh.indices[i + 2]};
        for (int e = 0; e < 3; ++e) {
            std::uint32_t a = t[e], b = t[(e + 1) % 3];
            if (a > b) std::swap(a, b);
            edges[{a, b}]++;
        }
    }
    bool closed = true;
    for (const auto& kv : edges)
        if (kv.second != 2) closed = false;
    CHECK(closed, "surface is a closed 2-manifold");

    // Normals point outward: dot(normal, vertex - centroid) > 0 for the vast
    // majority (a convex box).
    float cx = 0, cy = 0, cz = 0;
    const int vc = mesh.vertex_count();
    for (int i = 0; i < vc; ++i) {
        cx += mesh.vertices[i * 3];
        cy += mesh.vertices[i * 3 + 1];
        cz += mesh.vertices[i * 3 + 2];
    }
    cx /= vc; cy /= vc; cz /= vc;
    int outward = 0;
    for (int i = 0; i < vc; ++i) {
        const float dx = mesh.vertices[i * 3] - cx;
        const float dy = mesh.vertices[i * 3 + 1] - cy;
        const float dz = mesh.vertices[i * 3 + 2] - cz;
        const float dot = dx * mesh.normals[i * 3] + dy * mesh.normals[i * 3 + 1] +
                          dz * mesh.normals[i * 3 + 2];
        if (dot > 0) ++outward;
    }
    CHECK(outward >= vc * 9 / 10, "normals point outward for a convex box");

    // Empty mask -> no triangles.
    LabelVolume empty;
    empty.reset_to(v);
    Mesh none;
    CHECK(marching_cubes(empty.data(), 12, 12, 12, 1, 1, 1, 0, 1, none) == 0,
          "empty mask yields no surface");
}

// 7. binary STL size law: 84 + 50 * triangles; bad path errors.
static void test_stl() {
    std::printf("write_binary_stl\n");
    Volume v = make_volume(12, 0.0f);
    LabelVolume mask;
    mask.reset_to(v);
    for (int z = 3; z <= 8; ++z)
        for (int y = 3; y <= 8; ++y)
            for (int x = 3; x <= 8; ++x) mask.set(x, y, z, kActiveLabel);
    Mesh mesh;
    const int tris = marching_cubes(mask.data(), 12, 12, 12, 1, 1, 1, 0, 1, mesh);

    const char* path = "/tmp/lumenslice_segtest.stl";
    CHECK(write_binary_stl(mesh, path) == 0, "STL writes OK");
    std::FILE* fp = std::fopen(path, "rb");
    CHECK(fp != nullptr, "STL file exists");
    if (fp != nullptr) {
        std::fseek(fp, 0, SEEK_END);
        const long size = std::ftell(fp);
        std::fclose(fp);
        CHECK(size == 84 + 50L * tris, "STL size == 84 + 50 * triangles");
    }
    CHECK(write_binary_stl(mesh, "/no/such/dir/x.stl") != 0, "bad path errors");
}

// 8. Multi-segment: edits target the active label and never disturb others.
static void test_multi_segment() {
    std::printf("multi-segment isolation\n");
    Volume v = make_volume(8, 0.0f);
    LabelVolume mask;
    mask.reset_to(v);

    // Segment 1 paints a disk; segment 2 paints an overlapping disk.
    paint_disk(v, Axis::Axial, 2, 2, 2, 1, true, mask, 1);
    const long c1 = mask.count_nonzero();
    CHECK(c1 == 5, "segment 1 paints 5 voxels");

    paint_disk(v, Axis::Axial, 2, 5, 5, 1, true, mask, 2);
    CHECK(mask.at(2, 2, 2) == 1, "segment 1 voxel keeps its id");
    CHECK(mask.at(5, 5, 2) == 2, "segment 2 voxel has its own id");

    // Erasing segment 2 must not remove segment 1's voxels.
    paint_disk(v, Axis::Axial, 2, 2, 2, 1, false, mask, 2); // erase seg2 over seg1 area
    CHECK(mask.at(2, 2, 2) == 1, "erasing seg 2 leaves seg 1 intact");

    // Threshold on segment 3 over a uniform 0-HU volume claims only background.
    threshold_fill(v, -1.0f, 1.0f, mask, 3);
    CHECK(mask.at(2, 2, 2) == 1, "threshold seg 3 does not steal seg 1");
    CHECK(mask.at(5, 5, 2) == 2, "threshold seg 3 does not steal seg 2");
    CHECK(mask.at(0, 0, 0) == 3, "threshold seg 3 claims background");
}

// 9. SegmentTable id allocation, removal, and active fallback.
static void test_segment_table() {
    std::printf("segment table\n");
    SegmentTable t;
    const std::uint8_t a = t.add(Rgb{255, 0, 0});
    const std::uint8_t b = t.add(Rgb{0, 255, 0});
    CHECK(a == 1 && b == 2, "ids allocate densely from 1");
    CHECK(t.active() == 2, "newest segment is active");
    CHECK(t.count() == 2, "two segments registered");

    t.remove(1); // reuse id 1 next
    const std::uint8_t c = t.add(Rgb{0, 0, 255});
    CHECK(c == 1, "freed id is reused");
    CHECK(t.color(1).b == 255, "colour LUT updated");

    t.set_visible(1, false);
    CHECK(!t.visible(1), "visibility toggles");
    t.remove(2);
    t.remove(1);
    CHECK(t.count() == 0 && t.active() == 0, "emptied table has no active id");
}

// 10. Otsu separates a clear bimodal histogram between the two modes.
static void test_otsu() {
    std::printf("otsu threshold\n");
    Volume v = make_volume(8, -1000.0f); // background mode at -1000
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) set_hu(v, x, y, z, 1000.0f); // bright mode
    v.hu_min = -1000.0f;
    v.hu_max = 1000.0f;
    const float t = otsu_threshold(v);
    CHECK(t > -1000.0f && t < 1000.0f, "otsu lands between the two modes");
}

// 11. Islands: keep-largest drops the small blob; remove-small respects the cutoff.
static void test_islands() {
    std::printf("islands cleanup\n");
    Volume v = make_volume(10, 0.0f);
    LabelVolume mask;
    mask.reset_to(v);
    // Big blob (3x3x3 = 27) and a tiny blob (single voxel), same label, separated.
    for (int z = 1; z <= 3; ++z)
        for (int y = 1; y <= 3; ++y)
            for (int x = 1; x <= 3; ++x) mask.set(x, y, z, 1);
    mask.set(8, 8, 8, 1);
    CHECK(mask.count_nonzero() == 28, "two components, 28 voxels");

    LabelVolume copy = mask;
    const long removed = keep_largest_island(copy, 1);
    CHECK(removed == 1, "keep-largest removes the single-voxel blob");
    CHECK(copy.count_nonzero() == 27, "largest component survives");
    CHECK(copy.at(8, 8, 8) == 0, "tiny blob cleared");

    const long removed2 = remove_small_islands(mask, 1, 10);
    CHECK(removed2 == 1, "remove-small drops the sub-threshold blob");
    CHECK(mask.count_nonzero() == 27, "big blob above the cutoff survives");
}

// 12. Undo/redo round-trips a mutation and respects the depth cap.
static void test_undo() {
    std::printf("undo / redo\n");
    Volume v = make_volume(8, 0.0f);
    LabelVolume mask;
    mask.reset_to(v);
    UndoStack undo;

    undo.capture(mask);                 // state A: empty
    paint_disk(v, Axis::Axial, 2, 3, 3, 1, true, mask, 1); // -> state B
    CHECK(mask.count_nonzero() == 5, "painted 5");
    CHECK(undo.can_undo(), "undo available after capture");

    CHECK(undo.undo(mask), "undo applies");
    CHECK(mask.count_nonzero() == 0, "undo restores empty mask");
    CHECK(undo.can_redo(), "redo now available");

    CHECK(undo.redo(mask), "redo applies");
    CHECK(mask.count_nonzero() == 5, "redo restores painted mask");

    // Depth cap: more than kDepth captures keeps only the most recent kDepth.
    UndoStack capped;
    for (std::size_t i = 0; i < UndoStack::kDepth + 5; ++i) capped.capture(mask);
    int depth = 0;
    while (capped.undo(mask)) ++depth;
    CHECK(depth == static_cast<int>(UndoStack::kDepth),
          "history is capped at kDepth states");
}

// 13. Margin (grow/shrink) and smooth on the active label.
static void test_morphology() {
    std::printf("margin + smooth\n");
    Volume v = make_volume(9, 0.0f);

    // Solid 3x3x3 cube of label 1 at [3..5].
    LabelVolume cube;
    cube.reset_to(v);
    for (int z = 3; z <= 5; ++z)
        for (int y = 3; y <= 5; ++y)
            for (int x = 3; x <= 5; ++x) cube.set(x, y, z, 1);
    CHECK(cube.count_nonzero() == 27, "cube starts at 27 voxels");

    // Erode by 1: only the fully-interior centre voxel survives.
    LabelVolume eroded = cube;
    const long removed = erode_label(eroded, 1, 1);
    CHECK(removed == 26, "erode-1 peels the 26-voxel shell");
    CHECK(eroded.count_nonzero() == 1, "only the centre voxel remains");
    CHECK(eroded.at(4, 4, 4) == 1, "centre voxel is the survivor");

    // Dilate by 1: claims the face-adjacent background shell, leaves cube intact.
    LabelVolume grown = cube;
    const long added = dilate_label(grown, 1, 1);
    CHECK(added > 0, "dilate-1 adds voxels");
    CHECK(grown.at(2, 4, 4) == 1, "background just outside a face is claimed");
    CHECK(grown.at(4, 4, 4) == 1, "interior stays labelled");

    // Dilate never steals from another segment.
    LabelVolume two = cube;
    two.set(2, 4, 4, 2); // a segment-2 voxel touching the cube's face
    dilate_label(two, 1, 1);
    CHECK(two.at(2, 4, 4) == 2, "dilate does not overwrite another segment");

    // Smooth: a lone voxel is a minority everywhere and gets removed.
    LabelVolume lone;
    lone.reset_to(v);
    lone.set(4, 4, 4, 1);
    const long changed = smooth_label(lone, 1, 1);
    CHECK(changed == 1 && lone.count_nonzero() == 0, "smooth deletes a lone voxel");
}

// 14. Strategy effects apply the same as the raw kernels, through one interface.
static void test_effects() {
    std::printf("segmentation effects (Strategy)\n");
    Volume v = make_volume(8, 0.0f);
    LabelVolume mask;
    mask.reset_to(v);

    // Drive a concrete effect through a base-class reference: that is the whole
    // point of the pattern - the caller does not know which effect it holds.
    const PaintEffect paint{Axis::Axial, 2, 3, 3, 1, true};
    const SegmentationEffect& effect = paint;
    const long painted = effect.apply(v, mask, 1);
    CHECK(painted == 5, "PaintEffect paints a radius-1 disk (5 voxels)");
    CHECK(mask.at(3, 3, 2) == 1, "effect targeted the active label");

    // A second effect over the same mask, again through the base interface.
    const SmoothEffect smooth{1};
    const SegmentationEffect& smoother = smooth;
    smoother.apply(v, mask, 1); // rounds the lone-ish disk; just must not crash
    CHECK(mask.valid(), "SmoothEffect leaves a valid mask");
}

// 15. SegmentEditor facade: bound volume, undo round-trip, remove clears voxels.
static void test_segment_editor() {
    std::printf("segment editor (facade)\n");
    Volume v = make_volume(8, 0.0f);
    SegmentEditor editor;
    editor.reset_to(v, Rgb{0, 180, 210});
    CHECK(editor.segment_count() == 1 && editor.active() == 1,
          "fresh editor has one active segment");

    editor.push_undo();
    editor.paint(Axis::Axial, 2, 3, 3, 1, true); // 5 voxels into segment 1
    CHECK(editor.total_labelled() == 5, "paint via the editor labels 5 voxels");
    CHECK(editor.undo(), "undo applies");
    CHECK(editor.total_labelled() == 0, "undo restores the empty mask");

    // A second segment, painted, then removed - its voxels must vanish too.
    const std::uint8_t two = editor.add_segment(Rgb{200, 60, 60});
    CHECK(two == 2 && editor.active() == 2, "second segment is id 2 and active");
    editor.paint(Axis::Axial, 2, 5, 5, 1, true);
    CHECK(editor.label_count(2) == 5, "segment 2 has 5 voxels");
    editor.remove_segment(2);
    CHECK(editor.label_count(2) == 0 && editor.segment_count() == 1,
          "removing a segment clears its voxels and forgets it");
}

// 16. grow_from_seeds: two labels seeded either side of a sharp HU edge each fill
// their own basin and stop at the edge (tolerance-bounded competitive grow).
static void test_grow_from_seeds() {
    std::printf("-- grow_from_seeds\n");
    Volume v = make_volume(8, 0.0f);
    for (int z = 0; z < 8; ++z)
        for (int y = 0; y < 8; ++y)
            for (int x = 4; x < 8; ++x)
                set_hu(v, x, y, z, 1000.0f); // right half is a different tissue

    LabelVolume mask;
    mask.reset_to(v);
    mask.set(0, 0, 0, 1); // seed label 1 in the left (HU 0) basin
    mask.set(7, 7, 7, 2); // seed label 2 in the right (HU 1000) basin
    const long added = grow_from_seeds(v, mask, 100.0f);
    CHECK(added == 510, "grow fills both basins minus the two seeds");
    CHECK(mask.count_nonzero() == 512, "every voxel labelled");
    CHECK(mask.at(3, 4, 4) == 1, "left basin claimed by label 1");
    CHECK(mask.at(4, 4, 4) == 2, "right basin claimed by label 2 (edge respected)");

    // One seed + tight tolerance grows only its own basin; the edge stops it.
    LabelVolume mask2;
    mask2.reset_to(v);
    mask2.set(0, 0, 0, 1);
    const long one = grow_from_seeds(v, mask2, 100.0f);
    CHECK(one == 255, "one seed fills only its 256-voxel basin");
    CHECK(mask2.at(4, 0, 0) == 0, "grow stops at the edge, far basin stays bg");

    // No seeds -> no-op.
    LabelVolume mask3;
    mask3.reset_to(v);
    CHECK(grow_from_seeds(v, mask3, 100.0f) == 0, "no seeds is a no-op");
}

int main() {
    std::printf("== SegTest ==\n");
    test_plane_map_roundtrip();
    test_threshold();
    test_region_grow();
    test_paint();
    test_mask_overlay();
    test_marching_cubes();
    test_stl();
    test_multi_segment();
    test_segment_table();
    test_otsu();
    test_islands();
    test_undo();
    test_morphology();
    test_effects();
    test_segment_editor();
    test_grow_from_seeds();
    if (g_failures == 0) {
        std::printf("All segmentation tests passed.\n");
        return 0;
    }
    std::printf("%d assertion(s) failed.\n", g_failures);
    return 1;
}
