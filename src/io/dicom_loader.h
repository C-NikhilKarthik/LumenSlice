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

// One DICOM series discovered under a folder, from a fast header-only scan.
struct SeriesInfo {
    std::string uid;         // Series Instance UID (may be empty for unlabeled data)
    std::string description; // Series Description (may be empty)
    std::string modality;    // e.g. "CT", "MR"
    int slice_count = 0;     // number of DICOM-looking files in this series
};

// Load every usable DICOM slice under `folder` (searched recursively) into a
// single calibrated Volume. When a folder holds more than one series, this keeps
// the largest. Never throws; failures are reported in the result.
LoadResult LoadDicomFolder(const std::string& folder);

// Header-only scan (does not read pixel data): list the distinct DICOM series
// found under `folder`, ordered largest-first. Empty when nothing DICOM-like is
// found. Cheap enough to run before deciding which series to load.
std::vector<SeriesInfo> EnumerateSeries(const std::string& folder);

// Load exactly the series with Series Instance UID `series_uid` (as returned by
// EnumerateSeries; the empty string selects unlabeled slices). Same result shape
// as LoadDicomFolder.
LoadResult LoadDicomSeries(const std::string& folder, const std::string& series_uid);

} // namespace lumen
