// LumenSlice — binary STL export for the marching-cubes mesh.
//
// Binary STL: 80-byte header, uint32 triangle count, then per triangle a facet
// normal (3 floats), 3 vertices (9 floats), and a uint16 attribute count = 50
// bytes. Total file size is 84 + 50 * triangles. Coordinates are written in mm,
// exactly as the mesh holds them. macOS/arm64 is little-endian, which matches the
// STL byte order, so values are written directly.

#pragma once

#include "segmentation/marching_cubes.hpp"

namespace lumen {

// Write `mesh` to `path` as binary STL. Returns 0 on success, else a non-zero
// errno-style code (e.g. the open failure's errno, or EIO on a short write).
int write_binary_stl(const Mesh& mesh, const char* path);

} // namespace lumen
