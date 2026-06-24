import XCTest
@testable import LumenSlice

// Parsing of the metadata blob the C bridge emits (app/MetadataModels.swift).
// The escaping test is the consumer half of the round-trip the eng review
// flagged as a silent-corruption risk: the C++ serializer escapes special
// bytes, and this proves Swift reverses them to the original characters.
final class MetadataParseTests: XCTestCase {
    func testParsesCuratedFieldsAndTags() {
        let json = """
        {"meta":{"patient_name":"DOE^JANE","patient_id":"X1","modality":"CT",\
        "study_description":"HEAD","series_instance_uid":"1.2.3"},\
        "tags":[{"ge":"(0010,0010)","vr":"PN","name":"PatientName","value":"DOE^JANE"}]}
        """
        let m = DicomMetadata.parse(json)
        XCTAssertNotNil(m)
        XCTAssertEqual(m?.meta.patientName, "DOE^JANE")
        XCTAssertEqual(m?.meta.patientId, "X1")
        XCTAssertEqual(m?.meta.modality, "CT")
        XCTAssertEqual(m?.meta.studyDescription, "HEAD")
        XCTAssertEqual(m?.meta.seriesInstanceUid, "1.2.3")
        XCTAssertEqual(m?.tags.count, 1)
        XCTAssertEqual(m?.tags.first?.name, "PatientName")
        XCTAssertEqual(m?.tags.first?.ge, "(0010,0010)")
    }

    func testDecodesEscapedSpecialCharacters() {
        // JSON below contains \" \\ \t \n - the escapes the C++ side produces.
        let json = """
        {"meta":{"study_description":"A \\"quote\\", slash \\\\, tab\\t, nl\\n"},"tags":[]}
        """
        let m = DicomMetadata.parse(json)
        XCTAssertEqual(m?.meta.studyDescription, "A \"quote\", slash \\, tab\t, nl\n")
    }

    func testMissingFieldsDefaultToEmpty() {
        // The C++ serializer emits every meta key, but the parser is tolerant:
        // an absent key decodes to "" instead of failing the whole blob.
        let m = DicomMetadata.parse(#"{"meta":{"patient_name":"X"},"tags":[]}"#)
        XCTAssertEqual(m?.meta.patientName, "X")
        XCTAssertEqual(m?.meta.patientId, "")
        XCTAssertEqual(m?.tags.count, 0)
    }

    func testMalformedJsonReturnsNil() {
        XCTAssertNil(DicomMetadata.parse("{not json"))
        XCTAssertNil(DicomMetadata.parse(""))
    }
}
