// LumenSlice - DICOM metadata extraction (docs/agent.md SS1: UI-agnostic).
//
// Pulls the curated patient/study/series fields a viewer wants to show, plus a
// flat enumeration of every top-level element, out of a representative DICOM
// dataset - then serializes both to one JSON blob the Swift layer parses once.
// The voxel loader (dicom_loader) keeps pixels; this keeps the human-readable
// context the loader used to discard.

#pragma once

#include <string>
#include <vector>

class DcmItem; // DCMTK dataset/item; forward-declared to keep this header light.

namespace lumen {

// Curated, already-formatted study/patient metadata. Every field is a string so
// the bridge can serialize uniformly; an absent tag yields an empty string.
struct StudyMeta {
    // Patient.
    std::string patient_name;
    std::string patient_id;
    std::string patient_birth_date;
    std::string patient_sex;
    std::string patient_age;
    // Study.
    std::string study_date;
    std::string study_time;
    std::string study_description;
    std::string study_instance_uid;
    std::string accession_number;
    std::string referring_physician;
    // Series.
    std::string series_description;
    std::string series_instance_uid;
    std::string series_number;
    std::string modality;
    std::string body_part;
    // Equipment.
    std::string manufacturer;
    std::string model_name;
    std::string station_name;
    std::string institution_name;
    std::string software_versions;
};

// One enumerated element from the dataset's top level.
struct DicomTag {
    std::string group_element; // "(0010,0010)"
    std::string vr;            // "PN"
    std::string name;          // "PatientName" (or "Unknown" / private creator)
    std::string value;         // joined for multi-valued; "<N bytes>" for binary
};

// Extract the curated fields from `dataset`. Never throws; missing tags -> "".
[[nodiscard]] StudyMeta extract_study_meta(DcmItem& dataset);

// Enumerate every top-level element of `dataset`. Sequences are listed but not
// recursed in this version; binary/pixel elements report a byte count, not bytes.
// Never throws.
[[nodiscard]] std::vector<DicomTag> enumerate_tags(DcmItem& dataset);

// True when extraction yielded anything worth showing: at least one tag, or at
// least one non-empty curated field. Lets callers leave the metadata blob empty
// (rather than emitting an all-empty JSON object) when extraction failed.
[[nodiscard]] bool metadata_present(const StudyMeta& meta,
                                    const std::vector<DicomTag>& tags);

// Serialize curated meta + tag list to one JSON object string. All values are
// JSON-escaped (quotes, backslashes, control chars), so arbitrary DICOM byte
// values round-trip safely through the Swift parser.
[[nodiscard]] std::string serialize_meta_json(const StudyMeta& meta,
                                              const std::vector<DicomTag>& tags);

} // namespace lumen
