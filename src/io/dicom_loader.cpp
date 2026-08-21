#include "io/dicom_loader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
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

// The DICM preamble is optional in the DICOM standard. Many PACS exports use
// the conventional .dcm/.dicom extension but omit the 128-byte preamble, so
// retain those files for DCMTK's parser instead of discarding them up front.
bool HasDicomExtension(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".dcm" || ext == ".dicom";
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
    // usesEncapsulatedFormat() is the modern spelling (DCMTK >= 3.6.9, e.g. the
    // 3.7.0 Homebrew keg on macOS). vcpkg pins 3.6.8 for the Windows build, where
    // that method does not exist yet and isEncapsulated() is the equivalent (it is
    // only deprecated, not removed, in the newer releases).
#if defined(PACKAGE_VERSION_NUMBER) && PACKAGE_VERSION_NUMBER >= 369
    if (xfer.usesEncapsulatedFormat()) {
#else
    if (xfer.isEncapsulated()) {
#endif
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

// Crawl `folder` for files that look like DICOM (DICM magic or .dcm/.dicom
// extension). Shared by the full load and the header-only series scan.
std::vector<fs::path> CollectCandidates(const std::string& folder, int* files_scanned) {
    std::vector<fs::path> candidates;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             folder, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (files_scanned != nullptr) ++(*files_scanned);
        if (HasDicmSignature(it->path()) || HasDicomExtension(it->path()))
            candidates.push_back(it->path());
    }
    return candidates;
}

// Normalize a DICOM DA date ("YYYYMMDD") to "YYYY-MM-DD" for display. Anything
// that is not 8 digits is passed through unchanged (some files omit the date).
std::string NormalizeDicomDate(const std::string& raw) {
    if (raw.size() != 8) return raw;
    for (char c : raw)
        if (c < '0' || c > '9') return raw;
    return raw.substr(0, 4) + "-" + raw.substr(4, 2) + "-" + raw.substr(6, 2);
}

// Read only the series-identifying header tags (plus per-slice geometry and an
// acquisition date). Stops before PixelData, so this never reads (or decompresses)
// the image itself - cheap enough to run over an entire folder just to list the
// series. Returns false if the header won't parse.
bool ReadSeriesId(const fs::path& path, SeriesInfo& info) {
    DcmFileFormat ff;
    if (ff.loadFileUntilTag(path.string().c_str(), EXS_Unknown, EGL_noChange,
                            DCM_MaxReadLength, ERM_autoDetect, DCM_PixelData)
            .bad()) {
        return false;
    }
    DcmDataset* ds = ff.getDataset();
    if (ds == nullptr) return false;
    OFString s;
    if (ds->findAndGetOFString(DCM_SeriesInstanceUID, s).good()) info.uid = s.c_str();
    if (ds->findAndGetOFString(DCM_SeriesDescription, s).good()) info.description = s.c_str();
    if (ds->findAndGetOFString(DCM_Modality, s).good()) info.modality = s.c_str();
    Uint16 u16 = 0;
    if (ds->findAndGetUint16(DCM_Columns, u16).good()) info.width = u16;
    if (ds->findAndGetUint16(DCM_Rows, u16).good()) info.height = u16;
    // Prefer the series date, then study, then acquisition - whichever the file
    // actually carries. Displayed as the series' "Date Created".
    if (ds->findAndGetOFString(DCM_SeriesDate, s).good() && !s.empty())
        info.created = NormalizeDicomDate(s.c_str());
    else if (ds->findAndGetOFString(DCM_StudyDate, s).good() && !s.empty())
        info.created = NormalizeDicomDate(s.c_str());
    else if (ds->findAndGetOFString(DCM_AcquisitionDate, s).good() && !s.empty())
        info.created = NormalizeDicomDate(s.c_str());
    return true;
}

} // namespace

// Shared body. `filter_uid == nullptr` keeps the largest series (the historical
// behaviour of LoadDicomFolder); a non-null pointer keeps exactly that Series
// Instance UID (including the empty string, i.e. unlabeled slices).
static LoadResult LoadDicomImpl(const std::string& folder, const std::string* filter_uid) {
    LoadResult result;
    EnsureCodecsRegistered();

    std::error_code ec;
    if (!fs::exists(folder, ec) || !fs::is_directory(folder, ec)) {
        result.message = "Not a folder: " + folder;
        return result;
    }

    std::vector<fs::path> candidates = CollectCandidates(folder, &result.files_scanned);
    if (candidates.empty()) {
        result.message = "No DICOM files found under " + folder;
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
    // and Z-ordering, so we load a single series: either the caller's chosen one or
    // (by default) the largest by Series Instance UID.
    {
        if (filter_uid != nullptr) {
            slices.erase(std::remove_if(slices.begin(), slices.end(),
                                        [&](const Slice& s) {
                                            return s.series_uid != *filter_uid;
                                        }),
                         slices.end());
            if (slices.empty()) {
                result.message = "The selected series has no usable slices.";
                return result;
            }
        } else {
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
            }
        }
        // slices_loaded / files_skipped are reconciled by the dimension-lock
        // block below, which recomputes them from the final slice count.
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

LoadResult LoadDicomFolder(const std::string& folder) {
    return LoadDicomImpl(folder, nullptr);
}

LoadResult LoadDicomSeries(const std::string& folder, const std::string& series_uid) {
    return LoadDicomImpl(folder, &series_uid);
}

std::vector<SeriesInfo> EnumerateSeries(const std::string& folder) {
    EnsureCodecsRegistered();
    std::vector<SeriesInfo> out;
    std::error_code ec;
    if (!fs::exists(folder, ec) || !fs::is_directory(folder, ec)) return out;

    // Group cheap header-only reads by Series Instance UID: remember the first
    // description / modality / geometry / date seen per series and count its slices.
    std::unordered_map<std::string, SeriesInfo> by_uid;
    for (const auto& path : CollectCandidates(folder, nullptr)) {
        SeriesInfo slice_info;
        if (!ReadSeriesId(path, slice_info)) continue;
        SeriesInfo& info = by_uid[slice_info.uid];
        // The first slice of a series defines its display metadata; later slices
        // only bump the count (slice_info.slice_count is always 0 from the reader).
        if (info.slice_count == 0) info = slice_info;
        ++info.slice_count;
    }

    out.reserve(by_uid.size());
    for (auto& kv : by_uid) out.push_back(std::move(kv.second));
    // Largest series first: almost always the primary scan the user wants.
    std::sort(out.begin(), out.end(), [](const SeriesInfo& a, const SeriesInfo& b) {
        return a.slice_count > b.slice_count;
    });
    return out;
}

} // namespace lumen
