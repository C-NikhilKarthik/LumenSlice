#include "segmentation/marching_cubes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <vector>

#include "segmentation/marching_cubes_tables.hpp"

namespace lumen {
namespace {

constexpr float kIso = 0.5f;

// Corner local offsets (standard MC numbering).
constexpr std::array<std::array<int, 3>, 8> kCorner = {{
    {{0, 0, 0}}, {{1, 0, 0}}, {{1, 1, 0}}, {{0, 1, 0}},
    {{0, 0, 1}}, {{1, 0, 1}}, {{1, 1, 1}}, {{0, 1, 1}},
}};

// Edge -> (cornerA, cornerB).
constexpr std::array<std::array<int, 2>, 12> kEdge = {{
    {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}}, {{4, 5}}, {{5, 6}},
    {{6, 7}}, {{7, 4}}, {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
}};

// Build a downsampled 0/1 scalar field from the mask (nearest sample), then run
// `smooth_iters` of a light 6-neighbour blur to soften the voxel stair-steps.
struct Field {
    int w = 0, h = 0, d = 0;
    std::vector<float> v;
    [[nodiscard]] std::size_t idx(int i, int j, int k) const {
        return static_cast<std::size_t>(i) +
               static_cast<std::size_t>(j) * w +
               static_cast<std::size_t>(k) * static_cast<std::size_t>(w) * h;
    }
};

Field build_field(const std::uint8_t* mask, int mw, int mh, int md, int ds,
                  int smooth_iters) {
    Field f;
    f.w = (mw + ds - 1) / ds;
    f.h = (mh + ds - 1) / ds;
    f.d = (md + ds - 1) / ds;
    f.v.assign(static_cast<std::size_t>(f.w) * f.h * f.d, 0.0f);
    for (int k = 0; k < f.d; ++k) {
        const int sz = std::min(k * ds, md - 1);
        for (int j = 0; j < f.h; ++j) {
            const int sy = std::min(j * ds, mh - 1);
            for (int i = 0; i < f.w; ++i) {
                const int sx = std::min(i * ds, mw - 1);
                const std::size_t mi = static_cast<std::size_t>(sx) +
                                       static_cast<std::size_t>(sy) * mw +
                                       static_cast<std::size_t>(sz) *
                                           static_cast<std::size_t>(mw) * mh;
                f.v[f.idx(i, j, k)] = mask[mi] != 0 ? 1.0f : 0.0f;
            }
        }
    }

    for (int pass = 0; pass < smooth_iters; ++pass) {
        std::vector<float> out(f.v.size());
        for (int k = 0; k < f.d; ++k)
            for (int j = 0; j < f.h; ++j)
                for (int i = 0; i < f.w; ++i) {
                    float sum = f.v[f.idx(i, j, k)];
                    int n = 1;
                    const int di[6] = {1, -1, 0, 0, 0, 0};
                    const int dj[6] = {0, 0, 1, -1, 0, 0};
                    const int dk[6] = {0, 0, 0, 0, 1, -1};
                    for (int e = 0; e < 6; ++e) {
                        const int ni = i + di[e], nj = j + dj[e], nk = k + dk[e];
                        if (ni < 0 || nj < 0 || nk < 0 || ni >= f.w ||
                            nj >= f.h || nk >= f.d)
                            continue;
                        sum += f.v[f.idx(ni, nj, nk)];
                        ++n;
                    }
                    out[f.idx(i, j, k)] = sum / static_cast<float>(n);
                }
        f.v.swap(out);
    }
    return f;
}

} // namespace

int marching_cubes(const std::uint8_t* mask, int w, int h, int d, float sx,
                   float sy, float sz, int smooth_iters, int downsample,
                   Mesh& out) {
    out.clear();
    if (mask == nullptr || w <= 1 || h <= 1 || d <= 1) return 0;
    const int ds = std::max(1, downsample);
    const Field f = build_field(mask, w, h, d, ds, std::max(0, smooth_iters));
    if (f.w < 2 || f.h < 2 || f.d < 2) return 0;

    const std::size_t field_count = f.v.size();
    const float step_x = ds * sx, step_y = ds * sy, step_z = ds * sz;

    // Weld vertices by edge identity (the unordered pair of field-grid corner
    // indices) so a vertex shared between cells is emitted once.
    std::unordered_map<std::uint64_t, std::uint32_t> edge_to_vertex;
    edge_to_vertex.reserve(field_count);

    auto corner_index = [&](int ci, int cj, int ck, int corner) {
        const auto& o = kCorner[static_cast<std::size_t>(corner)];
        return f.idx(ci + o[0], cj + o[1], ck + o[2]);
    };

    auto vertex_for_edge = [&](int ci, int cj, int ck, int edge) -> std::uint32_t {
        const int ca = kEdge[static_cast<std::size_t>(edge)][0];
        const int cb = kEdge[static_cast<std::size_t>(edge)][1];
        const std::size_t ia = corner_index(ci, cj, ck, ca);
        const std::size_t ib = corner_index(ci, cj, ck, cb);
        const std::uint64_t lo = std::min(ia, ib);
        const std::uint64_t hi = std::max(ia, ib);
        const std::uint64_t key = lo * field_count + hi;
        auto it = edge_to_vertex.find(key);
        if (it != edge_to_vertex.end()) return it->second;

        // Interpolate the crossing position along the edge (in mm).
        const auto& oa = kCorner[static_cast<std::size_t>(ca)];
        const auto& ob = kCorner[static_cast<std::size_t>(cb)];
        const float fa = f.v[ia];
        const float fb = f.v[ib];
        float t = (fb == fa) ? 0.5f : (kIso - fa) / (fb - fa);
        t = std::clamp(t, 0.0f, 1.0f);
        const float ax = (ci + oa[0]) * step_x, bx = (ci + ob[0]) * step_x;
        const float ay = (cj + oa[1]) * step_y, by = (cj + ob[1]) * step_y;
        const float az = (ck + oa[2]) * step_z, bz = (ck + ob[2]) * step_z;
        const auto index = static_cast<std::uint32_t>(out.vertices.size() / 3);
        out.vertices.push_back(ax + t * (bx - ax));
        out.vertices.push_back(ay + t * (by - ay));
        out.vertices.push_back(az + t * (bz - az));
        out.normals.insert(out.normals.end(), {0.0f, 0.0f, 0.0f});
        edge_to_vertex.emplace(key, index);
        return index;
    };

    for (int ck = 0; ck < f.d - 1; ++ck) {
        for (int cj = 0; cj < f.h - 1; ++cj) {
            for (int ci = 0; ci < f.w - 1; ++ci) {
                int cube = 0;
                for (int c = 0; c < 8; ++c) {
                    if (f.v[corner_index(ci, cj, ck, c)] >= kIso) cube |= (1 << c);
                }
                const std::uint16_t edges = kEdgeTable[static_cast<std::size_t>(cube)];
                if (edges == 0) continue;

                std::array<std::uint32_t, 12> vert{};
                for (int e = 0; e < 12; ++e) {
                    if (edges & (1 << e)) vert[static_cast<std::size_t>(e)] =
                        vertex_for_edge(ci, cj, ck, e);
                }

                const auto& row = kTriTable[static_cast<std::size_t>(cube)];
                for (int t = 0; row[static_cast<std::size_t>(t)] != -1; t += 3) {
                    // Emit with reversed winding: with "inside" = field >= iso,
                    // the Lorensen table's order yields inward-facing normals, so
                    // swap the last two indices to face the surface outward. This
                    // flips both the welded vertex normals and the STL facet
                    // normals consistently.
                    out.indices.push_back(vert[static_cast<std::size_t>(row[t])]);
                    out.indices.push_back(vert[static_cast<std::size_t>(row[t + 2])]);
                    out.indices.push_back(vert[static_cast<std::size_t>(row[t + 1])]);
                }
            }
        }
    }

    // Smooth per-vertex normals: accumulate each triangle's geometric normal into
    // its three vertices, then normalize.
    for (std::size_t i = 0; i + 2 < out.indices.size(); i += 3) {
        const std::uint32_t a = out.indices[i];
        const std::uint32_t b = out.indices[i + 1];
        const std::uint32_t c = out.indices[i + 2];
        const float* pa = &out.vertices[a * 3];
        const float* pb = &out.vertices[b * 3];
        const float* pc = &out.vertices[c * 3];
        const float ux = pb[0] - pa[0], uy = pb[1] - pa[1], uz = pb[2] - pa[2];
        const float vx = pc[0] - pa[0], vy = pc[1] - pa[1], vz = pc[2] - pa[2];
        const float nx = uy * vz - uz * vy;
        const float ny = uz * vx - ux * vz;
        const float nz = ux * vy - uy * vx;
        for (std::uint32_t idx : {a, b, c}) {
            out.normals[idx * 3 + 0] += nx;
            out.normals[idx * 3 + 1] += ny;
            out.normals[idx * 3 + 2] += nz;
        }
    }
    for (std::size_t i = 0; i < out.normals.size(); i += 3) {
        const float nx = out.normals[i], ny = out.normals[i + 1], nz = out.normals[i + 2];
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-12f) {
            out.normals[i] = nx / len;
            out.normals[i + 1] = ny / len;
            out.normals[i + 2] = nz / len;
        }
    }

    return out.triangle_count();
}

} // namespace lumen
