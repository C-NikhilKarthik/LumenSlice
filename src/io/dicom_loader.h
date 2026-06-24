// LumenSlice — DICOM ingestion (docs/plan.md §1, docs/dependencies.md "DCMTK").
//
// Crawls a folder, keeps only real DICOM files (verified by the "DICM" magic at
// byte 128), parses the pixel data via DCMTK's `dcmdata`, calibrates every voxel
// to Hounsfield Units, and geometrically sorts the slices along the scan normal
// so Z ordering is correct regardless of file naming.

#pragma once

#include <string>
#include <vector>

#include "core/volume.h"
#include "io/dicom_meta.hpp"

namespace lumen {

struct LoadResult {
    bool ok = false;
    Volume volume;
    std::string message;     // human-readable status / error
    int files_scanned = 0;   // regular files visited during the crawl
    int slices_loaded = 0;   // files that parsed into usable slices
    int files_skipped = 0;   // DICOM-looking files we couldn't use (compressed, mismatched, …)

    // Context pulled from the representative slice (identical across a series):
    // the curated fields and the full top-level tag enumeration. Empty on failure.
    StudyMeta meta;
    std::vector<DicomTag> tags;
};

// Load every usable DICOM slice under `folder` (searched recursively) into a
// single calibrated Volume. Never throws; failures are reported in the result.
LoadResult LoadDicomFolder(const std::string& folder);

} // namespace lumen
