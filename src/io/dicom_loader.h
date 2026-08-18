// LumenSlice — DICOM ingestion (docs/plan.md §1, docs/dependencies.md "DCMTK").
//
// Crawls a folder, keeping DICOM files with either the standard "DICM" magic or
// a conventional .dcm/.dicom extension (the preamble is optional). It parses
// pixel data via DCMTK, calibrates every voxel to Hounsfield Units, and sorts
// slices along the scan normal so Z ordering ignores file names.

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

struct DicomSeriesInfo {
    std::string uid;
    std::string description;
    int slices = 0;
    int width = 0;
    int height = 0;
};

// Load every usable DICOM slice under `folder` (searched recursively) into a
// single calibrated Volume. Never throws; failures are reported in the result.
LoadResult LoadDicomFolder(const std::string& folder);

// Same loader, restricted to one Series Instance UID. An empty UID preserves
// the legacy behavior of choosing the largest image series.
LoadResult LoadDicomFolder(const std::string& folder, const std::string& series_uid);

// Enumerate image series in a folder for a caller-facing scene picker.
std::vector<DicomSeriesInfo> ListDicomSeries(const std::string& folder);

} // namespace lumen
