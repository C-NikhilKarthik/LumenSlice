// StatsShot - headless statistics calibration on a real DICOM folder.
//
// Loads a folder, thresholds a HU band into one label, and prints the label-map
// measures plus the closed-surface measures across a sweep of smoothing levels, so
// the closed-surface smoothing can be calibrated against a reference (3D Slicer)
// without the GUI. Not shipped in the app; a dev/calibration tool.
//
//   swift run -c release StatsShot <dicom_folder> <lowHU> <highHU>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "bridge/mesh_crop.hpp"
#include "io/dicom_loader.h"
#include "segmentation/label_volume.hpp"
#include "segmentation/marching_cubes.hpp"
#include "segmentation/statistics.hpp"

using namespace lumen;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: StatsShot <dicom_folder> <lowHU> <highHU>\n");
        std::printf("       StatsShot synthetic 0 0   (in-memory solid-sphere check)\n");
        return 1;
    }
    const char* folder = argv[1];

    // Controlled diagnostic: a solid sphere of known analytic volume, so the
    // marching-cubes closed-surface volume can be checked without any DICOM.
    if (std::string(folder) == "synthetic") {
        const double PI = 3.14159265358979;
        // Case builder: fills a mask via a predicate, measures LM + closed surface.
        auto run = [&](const char* label, int N, auto inside, double analyticVol) {
            Volume vol;
            vol.width = vol.height = vol.depth = N;
            vol.spacing_x = vol.spacing_y = vol.spacing_z = 1.0f;
            vol.voxel_buffer = std::make_unique<float[]>(vol.voxel_count());
            for (std::size_t i = 0; i < vol.voxel_count(); ++i)
                vol.voxel_buffer[i] = 0.0f;
            LabelVolume mask;
            mask.reset_to(vol);
            for (int z = 0; z < N; ++z)
                for (int y = 0; y < N; ++y)
                    for (int x = 0; x < N; ++x)
                        if (inside(x, y, z)) mask.set(x, y, z, 1);
            const SegmentStats s = compute_label_stats(mask, vol, 1);
            std::vector<std::uint8_t> field;
            const auto reg = lumen_bridge_detail::crop_label_region(
                mask, [](std::uint8_t v) { return v == 1; }, field);
            Mesh mesh;
            marching_cubes(field.data(), reg.w, reg.h, reg.d, 1, 1, 1, 0, 1, mesh);
            const MeshMetrics m = compute_mesh_metrics(mesh);
            std::printf("%s: LM=%.0f mm3 (analytic %.0f), CS=%.0f mm3  ->  "
                        "CS is %.1f%% of LM   [tris=%d]\n",
                        label, s.volume_mm3, analyticVol, m.volume_mm3,
                        100.0 * m.volume_mm3 / s.volume_mm3, mesh.triangle_count());
        };
        // 1. small solid sphere (control).
        run("solid sphere r=40", 100, [&](int x, int y, int z) {
            double dx = x - 50.0, dy = y - 50.0, dz = z - 50.0;
            return dx * dx + dy * dy + dz * dz <= 40.0 * 40.0;
        }, 4.0 / 3.0 * PI * 40 * 40 * 40);
        // 2. LARGE solid sphere (~14M triangles) - tests scale/complexity.
        run("solid sphere r=150", 320, [&](int x, int y, int z) {
            double dx = x - 160.0, dy = y - 160.0, dz = z - 160.0;
            return dx * dx + dy * dy + dz * dz <= 150.0 * 150.0;
        }, 4.0 / 3.0 * PI * 150 * 150 * 150);
        // 3. THIN spherical shell (~2 voxels thick) - tests thin-structure collapse.
        run("thin shell r=60 t~2", 140, [&](int x, int y, int z) {
            double dx = x - 70.0, dy = y - 70.0, dz = z - 70.0;
            double d2 = dx * dx + dy * dy + dz * dz;
            return d2 <= 60.0 * 60.0 && d2 >= 58.0 * 58.0;
        }, 4.0 / 3.0 * PI * (60.0 * 60 * 60 - 58.0 * 58 * 58));
        // 4. BOUNDARY-TOUCHING solid box (fills x=0..49, touches 3 edges) - the case
        //    that leaked before the unconditional zero-pad. Should now be ~100%.
        run("boundary box 50^3 at edge", 80, [&](int x, int y, int z) {
            return x < 50 && y < 50 && z < 50;
        }, 50.0 * 50.0 * 50.0);
        return 0;
    }

    const float lo = static_cast<float>(std::atof(argv[2]));
    const float hi = static_cast<float>(std::atof(argv[3]));

    LoadResult r = LoadDicomFolder(folder);
    if (!r.ok) {
        std::printf("load failed: %s\n", r.message.c_str());
        return 1;
    }
    const Volume& vol = r.volume;
    std::printf("loaded %dx%dx%d  spacing %.4f/%.4f/%.4f mm  HU[%.0f..%.0f]\n",
                vol.width, vol.height, vol.depth, vol.spacing_x, vol.spacing_y,
                vol.spacing_z, vol.hu_min, vol.hu_max);

    // Threshold the HU band into label 1 (matches the app's Bone-preset segment).
    LabelVolume mask;
    mask.reset_to(vol);
    for (int z = 0; z < vol.depth; ++z)
        for (int y = 0; y < vol.height; ++y)
            for (int x = 0; x < vol.width; ++x) {
                const float hu = vol.voxel_buffer[vol.index(x, y, z)];
                if (hu >= lo && hu <= hi) mask.set(x, y, z, 1);
            }

    const SegmentStats s = compute_label_stats(mask, vol, 1);
    std::printf("\nLabel map (LM):\n");
    std::printf("  voxels        = %ld\n", s.voxel_count);
    std::printf("  volume (LM)   = %.2f cm3  (%.4e mm3)\n", s.volume_mm3 / 1000.0,
                s.volume_mm3);
    std::printf("  voxel-face SA = %.2f cm2  (%.4e mm2)\n",
                s.surface_area_mm2 / 100.0, s.surface_area_mm2);
    std::printf("  HU mean=%.1f  sd=%.1f  range=%.0f..%.0f\n", s.hu_mean,
                s.hu_stddev, s.hu_min, s.hu_max);

    // Closed surface at smooth 0 for the requested band, plus a light-smoothing
    // area sweep. The CS volume being far below the LM volume for a complex band is
    // the marching-cubes thin-structure collapse; the diagnostic below tests a
    // SOLID band to confirm MC volume is faithful when the region is not thin.
    std::vector<std::uint8_t> field;
    auto measure_cs = [&](const LabelVolume& m, int si) {
        const auto reg = lumen_bridge_detail::crop_label_region(
            m, [](std::uint8_t v) { return v == 1; }, field);
        Mesh mesh;
        const int tris = marching_cubes(field.data(), reg.w, reg.h, reg.d,
                                        vol.spacing_x, vol.spacing_y,
                                        vol.spacing_z, si, 1, mesh);
        const MeshMetrics mm = compute_mesh_metrics(mesh);
        std::printf("    smooth=%2d : CS vol=%8.2f cm3   CS surface=%9.2f cm2   tris=%d\n",
                    si, mm.volume_mm3 / 1000.0, mm.surface_area_mm2 / 100.0, tris);
    };
    std::printf("\nClosed surface (field-smoothing sweep, with zero-pad crop):\n");
    for (int si : {0, 1, 2, 4}) measure_cs(mask, si);
    std::printf("(Slicer reference for this segment: CS vol 3060.72 cm3, "
                "CS surface 18462.10 cm2)\n");

    // DIAGNOSTIC: a large SOLID band (soft tissue + bone, HU >= -300) fills the body
    // interior as one thick mass. If MC volume is faithful, CS vol here should track
    // LM vol; if it also collapses, the bug is not thinness.
    LabelVolume solid;
    solid.reset_to(vol);
    for (int z = 0; z < vol.depth; ++z)
        for (int y = 0; y < vol.height; ++y)
            for (int x = 0; x < vol.width; ++x) {
                const float hu = vol.voxel_buffer[vol.index(x, y, z)];
                if (hu >= -300.0f) solid.set(x, y, z, 1);
            }
    const SegmentStats ss = compute_label_stats(solid, vol, 1);
    std::printf("\nDIAGNOSTIC solid band (HU >= -300):\n");
    std::printf("  LM vol=%.2f cm3  voxels=%ld\n", ss.volume_mm3 / 1000.0,
                ss.voxel_count);
    measure_cs(solid, 0);
    return 0;
}
