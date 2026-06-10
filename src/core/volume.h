// LumenSlice — core volumetric data model.
//
// A scan is a single, flat, contiguous heap block of Hounsfield Units (see
// docs/context.md §1.1). We deliberately avoid per-slice pointer indirection:
// one `std::unique_ptr<float[]>` owns the whole volume and indices are mapped
// straight into the address space via pointer arithmetic.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace lumen {

// The three orthographic viewing planes (docs/context.md §1.2).
enum class Axis : int {
    Axial = 0,    // XY plane, stepped along Z
    Coronal = 1,  // XZ plane, stepped along Y
    Sagittal = 2, // YZ plane, stepped along X
};

// A loaded scan volume. Voxels hold real, rescale-calibrated HU values.
//
// Memory layout is strictly UI-agnostic (docs/agent.md §1): this struct holds
// raw buffers and dimensions only — no rendering or widget state.
struct Volume {
    // Dimensions in voxels.
    int width = 0;  // X (columns)
    int height = 0; // Y (rows)
    int depth = 0;  // Z (number of slices)

    // Physical voxel spacing in millimetres (dx, dy, dz).
    float spacing_x = 1.0f;
    float spacing_y = 1.0f;
    float spacing_z = 1.0f;

    // Calibrated HU values: HU = raw * RescaleSlope + RescaleIntercept.
    // Length == width * height * depth, laid out X-fastest then Y then Z.
    std::unique_ptr<float[]> voxel_buffer;

    // Observed HU range across the whole volume — handy for default windowing.
    float hu_min = 0.0f;
    float hu_max = 0.0f;

    size_t voxel_count() const {
        return static_cast<size_t>(width) * static_cast<size_t>(height) *
               static_cast<size_t>(depth);
    }

    bool valid() const { return voxel_buffer != nullptr && voxel_count() > 0; }

    // Linear Index = X + (Y * Width) + (Z * Width * Height).
    size_t index(int x, int y, int z) const {
        return static_cast<size_t>(x) +
               static_cast<size_t>(y) * static_cast<size_t>(width) +
               static_cast<size_t>(z) * static_cast<size_t>(width) *
                   static_cast<size_t>(height);
    }

    // Number of slices available when scrolling along the given axis.
    int slice_count(Axis axis) const {
        switch (axis) {
        case Axis::Axial: return depth;
        case Axis::Coronal: return height;
        case Axis::Sagittal: return width;
        }
        return 0;
    }
};

} // namespace lumen
