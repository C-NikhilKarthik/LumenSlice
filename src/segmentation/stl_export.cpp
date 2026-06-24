#include "segmentation/stl_export.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace lumen {
namespace {

bool write_floats(std::FILE* fp, const float* p, int n) {
    return std::fwrite(p, sizeof(float), static_cast<std::size_t>(n), fp) ==
           static_cast<std::size_t>(n);
}

} // namespace

int write_binary_stl(const Mesh& mesh, const char* path) {
    if (path == nullptr) return EINVAL;
    std::FILE* fp = std::fopen(path, "wb");
    if (fp == nullptr) return errno != 0 ? errno : EIO;

    unsigned char header[80] = {0};
    const char label[] = "LumenSlice binary STL";
    std::memcpy(header, label, sizeof(label) - 1);
    bool ok = std::fwrite(header, 1, 80, fp) == 80;

    const auto tris = static_cast<std::uint32_t>(mesh.triangle_count());
    ok = ok && std::fwrite(&tris, sizeof(tris), 1, fp) == 1;

    const std::uint16_t attr = 0;
    for (std::size_t i = 0; ok && i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = mesh.indices[i];
        const std::uint32_t b = mesh.indices[i + 1];
        const std::uint32_t c = mesh.indices[i + 2];
        const float* pa = &mesh.vertices[a * 3];
        const float* pb = &mesh.vertices[b * 3];
        const float* pc = &mesh.vertices[c * 3];

        // Facet normal (STL stores a per-triangle normal, recomputed here).
        const float ux = pb[0] - pa[0], uy = pb[1] - pa[1], uz = pb[2] - pa[2];
        const float vx = pc[0] - pa[0], vy = pc[1] - pa[1], vz = pc[2] - pa[2];
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-12f) { nx /= len; ny /= len; nz /= len; }
        const float normal[3] = {nx, ny, nz};

        ok = ok && write_floats(fp, normal, 3);
        ok = ok && write_floats(fp, pa, 3);
        ok = ok && write_floats(fp, pb, 3);
        ok = ok && write_floats(fp, pc, 3);
        ok = ok && std::fwrite(&attr, sizeof(attr), 1, fp) == 1;
    }

    const bool close_ok = std::fclose(fp) == 0;
    if (!ok || !close_ok) return EIO;
    return 0;
}

} // namespace lumen
