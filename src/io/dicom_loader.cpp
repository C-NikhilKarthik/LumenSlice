#include "io/dicom_loader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <vector>

// DCMTK — strictly the dcmdata layer (see docs/dependencies.md). We pull pixel
// data and the handful of geometry/rescale tags we need, then drop everything
// into our own flat float buffer.
#include "dcmtk/config/osconfig.h"
#include "dcmtk/dcmdata/dctk.h"
#include "dcmtk/dcmdata/dcxfer.h"
#include "dcmtk/dcmdata/dcrledrg.h" // RLE decoder registration
#include "dcmtk/dcmjpeg/djdecode.h" // JPEG (baseline/extended/lossless) decoders
#include "dcmtk/dcmjpls/djdecode.h" // JPEG-LS decoders

namespace fs = std::filesystem;

namespace lumen {
namespace {

// One parsed slice: HU pixels plus the geometry needed to order and scale it.
struct Slice {
    int rows = 0;
    int cols = 0;
    double spacing_row = 1.0; // mm between rows (Y)
    double spacing_col = 1.0; // mm between columns (X)
    std::array<double, 3> ipp{{0, 0, 0}};                 // Image Position (Patient)
    std::array<double, 6> iop{{1, 0, 0, 0, 1, 0}};        // Image Orientation (Patient)
    double sort_key = 0.0;                                // IPP projected on slice normal
    std::string series_uid;                               // groups slices into one series
    std::vector<float> hu;                                // length rows*cols
};

// Register DCMTK's pixel-data decoders exactly once. Without this, encapsulated
// (compressed) transfer syntaxes — JPEG, JPEG-LS, RLE, the formats most clinical
// PACS exports actually use — can't be decoded and every slice is skipped.
void EnsureCodecsRegistered() {
    static const bool registered = [] {
        DJDecoderRegistration::registerCodecs();     // JPEG
        DJLSDecoderRegistration::registerCodecs();   // JPEG-LS
        DcmRLEDecoderRegistration::registerCodecs(); // RLE
        return true;
    }();
    (void)registered;
}

// docs/plan.md §1: a real DICOM file carries the 4-byte "DICM" magic at offset
// 128 (right after the 128-byte preamble). Cheap pre-filter before we hand the
// file to DCMTK's heavier parser.
bool HasDicmSignature(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(128, std::ios::beg);
    char magic[4] = {0, 0, 0, 0};
    if (!f.read(magic, 4)) return false;
    return magic[0] == 'D' && magic[1] == 'I' && magic[2] == 'C' && magic[3] == 'M';
}

// Pull a single slice out of one DICOM dataset. Returns false (with the file
// counted as skipped) for anything we can't turn into HU pixels in Phase 1.
bool ParseSlice(const fs::path& path, Slice& out) {
    DcmFileFormat ff;
    if (ff.loadFile(path.string().c_str()).bad()) return false;
    DcmDataset* ds = ff.getDataset();
    if (ds == nullptr) return false;

    // Encapsulated (compressed) pixel data — JPEG / JPEG-LS / RLE — is decoded to
    // native little-endian in place so the uncompressed path below sees plain
    // pixels. Anything we still can't decode (e.g. a codec not registered) is
    // skipped rather than read as garbage.
    DcmXfer xfer(ds->getOriginalXfer());
    if (xfer.usesEncapsulatedFormat()) {
        if (ds->chooseRepresentation(EXS_LittleEndianExplicit, nullptr).bad() ||
            !ds->canWriteXfer(EXS_LittleEndianExplicit)) {
            return false;
        }
    }

    Uint16 rows = 0, cols = 0;
    if (ds->findAndGetUint16(DCM_Rows, rows).bad() ||
        ds->findAndGetUint16(DCM_Columns, cols).bad() || rows == 0 || cols == 0) {
        return false;
    }

    Uint16 bits_allocated = 16, pixel_rep = 0;
    ds->findAndGetUint16(DCM_BitsAllocated, bits_allocated);
    ds->findAndGetUint16(DCM_PixelRepresentation, pixel_rep);
    if (bits_allocated != 16) return false; // Phase 1: 16-bit grayscale (CT/MR).

    // Rescale to HU: default slope/intercept leave raw values untouched.
    Float64 slope = 1.0, intercept = 0.0;
    ds->findAndGetFloat64(DCM_RescaleSlope, slope);
    ds->findAndGetFloat64(DCM_RescaleIntercept, intercept);

    // Geometry tags. Missing values fall back to sane defaults so a lone test
    // slice still loads (it just won't sort meaningfully).
    out.rows = rows;
    out.cols = cols;
    OFString series_uid;
    if (ds->findAndGetOFString(DCM_SeriesInstanceUID, series_uid).good()) {
        out.series_uid = series_uid.c_str();
    }
    ds->findAndGetFloat64(DCM_PixelSpacing, out.spacing_row, 0); // row spacing (Y)
    ds->findAndGetFloat64(DCM_PixelSpacing, out.spacing_col, 1); // column spacing (X)
    for (unsigned long i = 0; i < 3; ++i)
        ds->findAndGetFloat64(DCM_ImagePositionPatient, out.ipp[i], i);
    for (unsigned long i = 0; i < 6; ++i)
        ds->findAndGetFloat64(DCM_ImageOrientationPatient, out.iop[i], i);

    const Uint16* pixels = nullptr;
    unsigned long count = 0;
    if (ds->findAndGetUint16Array(DCM_PixelData, pixels, &count).bad() ||
        pixels == nullptr) {
        return false;
    }
    const size_t expected = static_cast<size_t>(rows) * static_cast<size_t>(cols);
    if (count < expected) return false; // truncated / multi-frame: skip in Phase 1.

    out.hu.resize(expected);
    for (size_t i = 0; i < expected; ++i) {
        // Reinterpret the 16-bit sample per Pixel Representation, then calibrate.
        const float raw = pixel_rep ? static_cast<float>(static_cast<int16_t>(pixels[i]))
                                    : static_cast<float>(pixels[i]);
        out.hu[i] = raw * static_cast<float>(slope) + static_cast<float>(intercept);
    }
    return true;
}

} // namespace

LoadResult LoadDicomFolder(const std::string& folder) {
    LoadResult result;
    EnsureCodecsRegistered();

    std::error_code ec;
    if (!fs::exists(folder, ec) || !fs::is_directory(folder, ec)) {
        result.message = "Not a folder: " + folder;
        return result;
    }

    // Crawl: collect every regular file that carries the DICM signature.
    std::vector<fs::path> candidates;
    for (auto it = fs::recursive_directory_iterator(
             folder, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        ++result.files_scanned;
        if (HasDicmSignature(it->path())) candidates.push_back(it->path());
    }

    if (candidates.empty()) {
        result.message = "No DICOM files (DICM signature) found under " + folder;
        return result;
    }

    // Patient/study/series metadata is identical across every slice in a series,
    // so we only need one file's path to re-open for metadata later. Remember the
    // first that parses rather than carrying a heap-allocated path on every Slice.
    std::vector<Slice> slices;
    slices.reserve(candidates.size());
    fs::path representative_path;
    for (const auto& path : candidates) {
        Slice s;
        if (ParseSlice(path, s)) {
            if (representative_path.empty()) representative_path = path;
            slices.push_back(std::move(s));
            ++result.slices_loaded;
        } else {
            ++result.files_skipped;
        }
    }

    if (slices.empty()) {
        result.message = "Found DICOM files but none were usable (unsupported pixel "
                         "format, or compression this build can't decode).";
        return result;
    }

    // A folder often holds more than one series (the CT plus a scout/topogram, a
    // dose report, or a secondary capture). Merging them yields nonsensical spacing
    // and Z-ordering, so keep only the largest series (by Series Instance UID).
    {
        std::unordered_map<std::string, int> counts;
        for (const auto& s : slices) ++counts[s.series_uid];
        if (counts.size() > 1) {
            const std::string& best =
                std::max_element(counts.begin(), counts.end(),
                                 [](const auto& a, const auto& b) {
                                     return a.second < b.second;
                                 })
                    ->first;
            slices.erase(std::remove_if(slices.begin(), slices.end(),
                                        [&](const Slice& s) {
                                            return s.series_uid != best;
                                        }),
                         slices.end());
            // slices_loaded / files_skipped are reconciled by the dimension-lock
            // block below, which recomputes them from the final slice count.
        }
    }

    // Phase 1 expects one consistent series: lock dimensions to the first slice
    // and drop any that disagree.
    const int cols = slices.front().cols;
    const int rows = slices.front().rows;
    slices.erase(std::remove_if(slices.begin(), slices.end(),
                                [&](const Slice& s) {
                                    return s.cols != cols || s.rows != rows;
                                }),
                 slices.end());
    result.files_skipped += result.slices_loaded - static_cast<int>(slices.size());
    result.slices_loaded = static_cast<int>(slices.size());

    // Geometric Z-sort (docs/plan.md §1). The slice normal is the cross product
    // of the row and column direction cosines from Image Orientation (Patient);
    // projecting each Image Position onto it gives a true depth ordering that
    // ignores file names entirely.
    const auto& iop = slices.front().iop;
    const std::array<double, 3> row_dir{{iop[0], iop[1], iop[2]}};
    const std::array<double, 3> col_dir{{iop[3], iop[4], iop[5]}};
    std::array<double, 3> normal{{
        row_dir[1] * col_dir[2] - row_dir[2] * col_dir[1],
        row_dir[2] * col_dir[0] - row_dir[0] * col_dir[2],
        row_dir[0] * col_dir[1] - row_dir[1] * col_dir[0],
    }};
    const double nlen = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
                                  normal[2] * normal[2]);
    if (nlen > 1e-9) {
        normal[0] /= nlen;
        normal[1] /= nlen;
        normal[2] /= nlen;
    } else {
        normal = {{0.0, 0.0, 1.0}}; // degenerate orientation: fall back to raw Z.
    }
    for (auto& s : slices)
        s.sort_key = s.ipp[0] * normal[0] + s.ipp[1] * normal[1] + s.ipp[2] * normal[2];
    std::sort(slices.begin(), slices.end(),
              [](const Slice& a, const Slice& b) { return a.sort_key < b.sort_key; });

    // Assemble the contiguous voxel buffer.
    Volume vol;
    vol.width = cols;
    vol.height = rows;
    vol.depth = static_cast<int>(slices.size());
    vol.spacing_x = static_cast<float>(slices.front().spacing_col);
    vol.spacing_y = static_cast<float>(slices.front().spacing_row);
    // A series with absent/zero Pixel Spacing would otherwise render degenerate;
    // fall back to 1 mm isotropic so aspect math stays well-defined.
    if (!(vol.spacing_x > 0.0f)) vol.spacing_x = 1.0f;
    if (!(vol.spacing_y > 0.0f)) vol.spacing_y = 1.0f;

    // Z spacing from the gap between the first two sorted slice positions; if we
    // only have one slice, fall back to the in-plane spacing.
    if (slices.size() >= 2) {
        vol.spacing_z = static_cast<float>(std::abs(slices[1].sort_key - slices[0].sort_key));
    }
    if (!(vol.spacing_z > 0.0f)) vol.spacing_z = vol.spacing_y;

    vol.voxel_buffer = std::make_unique<float[]>(vol.voxel_count());
    float hu_min = std::numeric_limits<float>::max();
    float hu_max = std::numeric_limits<float>::lowest();
    const size_t slice_stride = static_cast<size_t>(cols) * static_cast<size_t>(rows);
    for (size_t z = 0; z < slices.size(); ++z) {
        float* dst = vol.voxel_buffer.get() + z * slice_stride;
        const std::vector<float>& src = slices[z].hu;
        for (size_t i = 0; i < slice_stride; ++i) {
            const float v = src[i];
            dst[i] = v;
            if (v < hu_min) hu_min = v;
            if (v > hu_max) hu_max = v;
        }
    }
    vol.hu_min = hu_min;
    vol.hu_max = hu_max;

    // Pull the human-readable context from a representative slice (patient,
    // study, series, equipment + the full top-level tag list). These are
    // identical across a series, so any successfully parsed file suffices. A
    // failure here is non-fatal: the volume still loads, just without metadata.
    {
        DcmFileFormat meta_ff;
        if (meta_ff.loadFile(representative_path.string().c_str()).good()) {
            if (DcmDataset* meta_ds = meta_ff.getDataset()) {
                result.meta = extract_study_meta(*meta_ds);
                result.tags = enumerate_tags(*meta_ds);
            }
        }
    }

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "Loaded %d slices (%dx%dx%d), spacing %.2f/%.2f/%.2f mm, HU [%.0f, %.0f]",
                  vol.depth, vol.width, vol.height, vol.depth, vol.spacing_x,
                  vol.spacing_y, vol.spacing_z, vol.hu_min, vol.hu_max);

    result.volume = std::move(vol);
    result.ok = true;
    result.message = buf;
    return result;
}

} // namespace lumen
