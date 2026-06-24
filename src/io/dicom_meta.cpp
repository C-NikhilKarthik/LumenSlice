#include "io/dicom_meta.hpp"

#include <cstdio>
#include <utility>

// DCMTK dcmdata: dataset/element types, the tag dictionary, and the DCM_* keys.
#include "dcmtk/config/osconfig.h"
#include "dcmtk/dcmdata/dctk.h"

namespace lumen {
namespace {

// Longest tag value we keep verbatim. Beyond this a value is truncated with an
// ellipsis - guards against a stray long text field bloating the JSON blob.
constexpr int kMaxValueChars = 2048;

// Read one element's full value (multi-valued joined with '\') as a string.
// Missing tag -> empty string.
std::string get_string(DcmItem& ds, const DcmTagKey& key) {
    OFString value;
    if (ds.findAndGetOFStringArray(key, value).good()) {
        return std::string(value.c_str());
    }
    return {};
}

// True for VRs whose payload is raw bytes we should not stringify (pixel data,
// other binary blobs). We report a byte count for these instead.
bool is_binary_vr(DcmEVR vr) {
    switch (vr) {
    case EVR_OB:
    case EVR_OW:
    case EVR_OF:
    case EVR_OD:
    case EVR_OL:
    case EVR_UN:
    case EVR_px: // internal: pixel data
    case EVR_ox: // internal: OB or OW (pixel/overlay data)
        return true;
    default:
        return false;
    }
}

// Append `s` to `out` as a JSON string literal, escaping per RFC 8259 so any
// DICOM byte value survives the round-trip to the Swift parser.
void append_json_string(std::string& out, const std::string& s) {
    out.push_back('"');
    for (const unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

// Append `"key":"value"` (value escaped). `trailing_comma` controls separation.
void append_pair(std::string& out, const char* key, const std::string& value,
                 bool trailing_comma) {
    append_json_string(out, key);
    out.push_back(':');
    append_json_string(out, value);
    if (trailing_comma) out.push_back(',');
}

} // namespace

StudyMeta extract_study_meta(DcmItem& dataset) {
    StudyMeta m;
    m.patient_name = get_string(dataset, DCM_PatientName);
    m.patient_id = get_string(dataset, DCM_PatientID);
    m.patient_birth_date = get_string(dataset, DCM_PatientBirthDate);
    m.patient_sex = get_string(dataset, DCM_PatientSex);
    m.patient_age = get_string(dataset, DCM_PatientAge);

    m.study_date = get_string(dataset, DCM_StudyDate);
    m.study_time = get_string(dataset, DCM_StudyTime);
    m.study_description = get_string(dataset, DCM_StudyDescription);
    m.study_instance_uid = get_string(dataset, DCM_StudyInstanceUID);
    m.accession_number = get_string(dataset, DCM_AccessionNumber);
    m.referring_physician = get_string(dataset, DCM_ReferringPhysicianName);

    m.series_description = get_string(dataset, DCM_SeriesDescription);
    m.series_instance_uid = get_string(dataset, DCM_SeriesInstanceUID);
    m.series_number = get_string(dataset, DCM_SeriesNumber);
    m.modality = get_string(dataset, DCM_Modality);
    m.body_part = get_string(dataset, DCM_BodyPartExamined);

    m.manufacturer = get_string(dataset, DCM_Manufacturer);
    m.model_name = get_string(dataset, DCM_ManufacturerModelName);
    m.station_name = get_string(dataset, DCM_StationName);
    m.institution_name = get_string(dataset, DCM_InstitutionName);
    m.software_versions = get_string(dataset, DCM_SoftwareVersions);
    return m;
}

std::vector<DicomTag> enumerate_tags(DcmItem& dataset) {
    std::vector<DicomTag> out;
    const unsigned long count = dataset.card();
    out.reserve(count);

    for (unsigned long i = 0; i < count; ++i) {
        DcmElement* elem = dataset.getElement(i);
        if (elem == nullptr) continue;

        // Non-const: DCMTK's getTagName() lazily resolves the dictionary and is
        // not marked const. This is a by-value copy, so mutating it is harmless.
        DcmTag tag = elem->getTag();
        DicomTag t;

        char ge[16];
        std::snprintf(ge, sizeof(ge), "(%04X,%04X)", tag.getGroup(),
                      tag.getElement());
        t.group_element = ge;
        t.vr = tag.getVR().getVRName();
        t.name = tag.getTagName(); // dictionary name, "Unknown", or private creator

        if (is_binary_vr(elem->getVR())) {
            t.value = "<" + std::to_string(elem->getLength()) + " bytes>";
        } else {
            OFString value;
            if (elem->getOFStringArray(value).good()) {
                t.value.assign(value.c_str());
                if (t.value.size() > static_cast<size_t>(kMaxValueChars)) {
                    t.value.resize(kMaxValueChars);
                    t.value += "...";
                }
            }
        }
        out.push_back(std::move(t));
    }
    return out;
}

bool metadata_present(const StudyMeta& meta, const std::vector<DicomTag>& tags) {
    if (!tags.empty()) return true;
    for (const std::string* field : {
             &meta.patient_name, &meta.patient_id, &meta.patient_birth_date,
             &meta.patient_sex, &meta.patient_age, &meta.study_date,
             &meta.study_time, &meta.study_description, &meta.study_instance_uid,
             &meta.accession_number, &meta.referring_physician,
             &meta.series_description, &meta.series_instance_uid,
             &meta.series_number, &meta.modality, &meta.body_part,
             &meta.manufacturer, &meta.model_name, &meta.station_name,
             &meta.institution_name, &meta.software_versions}) {
        if (!field->empty()) return true;
    }
    return false;
}

std::string serialize_meta_json(const StudyMeta& meta,
                                const std::vector<DicomTag>& tags) {
    std::string out;
    out.reserve(4096 + tags.size() * 64);

    out += "{\"meta\":{";
    append_pair(out, "patient_name", meta.patient_name, true);
    append_pair(out, "patient_id", meta.patient_id, true);
    append_pair(out, "patient_birth_date", meta.patient_birth_date, true);
    append_pair(out, "patient_sex", meta.patient_sex, true);
    append_pair(out, "patient_age", meta.patient_age, true);
    append_pair(out, "study_date", meta.study_date, true);
    append_pair(out, "study_time", meta.study_time, true);
    append_pair(out, "study_description", meta.study_description, true);
    append_pair(out, "study_instance_uid", meta.study_instance_uid, true);
    append_pair(out, "accession_number", meta.accession_number, true);
    append_pair(out, "referring_physician", meta.referring_physician, true);
    append_pair(out, "series_description", meta.series_description, true);
    append_pair(out, "series_instance_uid", meta.series_instance_uid, true);
    append_pair(out, "series_number", meta.series_number, true);
    append_pair(out, "modality", meta.modality, true);
    append_pair(out, "body_part", meta.body_part, true);
    append_pair(out, "manufacturer", meta.manufacturer, true);
    append_pair(out, "model_name", meta.model_name, true);
    append_pair(out, "station_name", meta.station_name, true);
    append_pair(out, "institution_name", meta.institution_name, true);
    append_pair(out, "software_versions", meta.software_versions, false);
    out += "},\"tags\":[";

    for (size_t i = 0; i < tags.size(); ++i) {
        const DicomTag& t = tags[i];
        out += "{";
        append_pair(out, "ge", t.group_element, true);
        append_pair(out, "vr", t.vr, true);
        append_pair(out, "name", t.name, true);
        append_pair(out, "value", t.value, false);
        out += "}";
        if (i + 1 < tags.size()) out.push_back(',');
    }
    out += "]}";
    return out;
}

} // namespace lumen
