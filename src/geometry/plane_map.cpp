#include "geometry/plane_map.hpp"

namespace lumen {

SliceDims slice_dims(const Volume& vol, Axis axis) {
    switch (axis) {
    case Axis::Axial:    return {vol.width, vol.height};
    case Axis::Coronal:  return {vol.width, vol.depth};
    case Axis::Sagittal: return {vol.height, vol.depth};
    }
    return {};
}

VoxelCoord plane_to_voxel(const Volume& vol, Axis axis, int index, int px,
                          int py) {
    const int zf = vol.depth - 1 - py; // vertical flip for coronal/sagittal
    switch (axis) {
    case Axis::Axial:    return {px, py, index};
    case Axis::Coronal:  return {px, index, zf};
    case Axis::Sagittal: return {index, px, zf};
    }
    return {};
}

PixelCoord voxel_to_plane(const Volume& vol, Axis axis, int x, int y, int z) {
    const int zf = vol.depth - 1 - z; // inverse of the same vertical flip
    switch (axis) {
    case Axis::Axial:    return {x, y};
    case Axis::Coronal:  return {x, zf};
    case Axis::Sagittal: return {y, zf};
    }
    return {};
}

} // namespace lumen
