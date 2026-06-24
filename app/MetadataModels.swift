import Foundation

// Swift mirror of the JSON the C bridge emits from lumen_meta_json:
//   {"meta":{...curated fields...},"tags":[{"ge","vr","name","value"},...]}
// Decoded once per load with .convertFromSnakeCase, so C++ `patient_name`
// maps to Swift `patientName`, `study_instance_uid` -> `studyInstanceUid`, etc.
struct DicomMetadata: Decodable {
    struct Meta: Decodable {
        // Patient.
        var patientName = ""
        var patientId = ""
        var patientBirthDate = ""
        var patientSex = ""
        var patientAge = ""
        // Study.
        var studyDate = ""
        var studyTime = ""
        var studyDescription = ""
        var studyInstanceUid = ""
        var accessionNumber = ""
        var referringPhysician = ""
        // Series.
        var seriesDescription = ""
        var seriesInstanceUid = ""
        var seriesNumber = ""
        var modality = ""
        var bodyPart = ""
        // Equipment.
        var manufacturer = ""
        var modelName = ""
        var stationName = ""
        var institutionName = ""
        var softwareVersions = ""

        enum CodingKeys: String, CodingKey {
            case patientName, patientId, patientBirthDate, patientSex, patientAge
            case studyDate, studyTime, studyDescription, studyInstanceUid
            case accessionNumber, referringPhysician
            case seriesDescription, seriesInstanceUid, seriesNumber, modality, bodyPart
            case manufacturer, modelName, stationName, institutionName, softwareVersions
        }

        // Tolerant decode: any absent key defaults to "" rather than throwing,
        // so partial or evolving metadata blobs still parse.
        init(from decoder: Decoder) throws {
            let c = try decoder.container(keyedBy: CodingKeys.self)
            func s(_ key: CodingKeys) -> String {
                (try? c.decode(String.self, forKey: key)) ?? ""
            }
            patientName = s(.patientName)
            patientId = s(.patientId)
            patientBirthDate = s(.patientBirthDate)
            patientSex = s(.patientSex)
            patientAge = s(.patientAge)
            studyDate = s(.studyDate)
            studyTime = s(.studyTime)
            studyDescription = s(.studyDescription)
            studyInstanceUid = s(.studyInstanceUid)
            accessionNumber = s(.accessionNumber)
            referringPhysician = s(.referringPhysician)
            seriesDescription = s(.seriesDescription)
            seriesInstanceUid = s(.seriesInstanceUid)
            seriesNumber = s(.seriesNumber)
            modality = s(.modality)
            bodyPart = s(.bodyPart)
            manufacturer = s(.manufacturer)
            modelName = s(.modelName)
            stationName = s(.stationName)
            institutionName = s(.institutionName)
            softwareVersions = s(.softwareVersions)
        }
    }

    struct Tag: Decodable {
        var ge = ""    // "(0010,0010)"
        var vr = ""    // "PN"
        var name = ""  // "PatientName"
        var value = ""

        enum CodingKeys: String, CodingKey { case ge, vr, name, value }

        init(from decoder: Decoder) throws {
            let c = try decoder.container(keyedBy: CodingKeys.self)
            ge = (try? c.decode(String.self, forKey: .ge)) ?? ""
            vr = (try? c.decode(String.self, forKey: .vr)) ?? ""
            name = (try? c.decode(String.self, forKey: .name)) ?? ""
            value = (try? c.decode(String.self, forKey: .value)) ?? ""
        }
    }

    var meta: Meta
    var tags: [Tag]

    // Parse a JSON blob from the bridge. Returns nil on malformed input.
    static func parse(_ json: String) -> DicomMetadata? {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        return try? decoder.decode(DicomMetadata.self, from: Data(json.utf8))
    }
}

// A tag paired with a stable identity for SwiftUI lists/tables. The group/element
// string is not guaranteed unique (private tags can repeat), so we key on the
// enumeration index instead.
struct IndexedTag: Identifiable {
    let id: Int
    let tag: DicomMetadata.Tag
}
