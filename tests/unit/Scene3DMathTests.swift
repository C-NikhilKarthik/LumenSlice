import XCTest
import simd
@testable import LumenSlice

// Pure camera geometry for the 3D view (app/ThreeD/Scene3DMath.swift + the standard
// view vectors). Pinned here without a live SCNView, the way the repo tests
// RangeSliderMath / SliceCoordinates.
final class Scene3DMathTests: XCTestCase {
    func testEncloseCentresAndSizesABox() {
        let pts = Scene3DMath.corners(min: SIMD3(-1, -2, -3), max: SIMD3(1, 2, 3))
        let s = Scene3DMath.enclose(pts)
        XCTAssertEqual(s?.center.x ?? .nan, 0, accuracy: 0.0001)
        XCTAssertEqual(s?.center.y ?? .nan, 0, accuracy: 0.0001)
        XCTAssertEqual(s?.center.z ?? .nan, 0, accuracy: 0.0001)
        // radius = half of the largest extent (z: 6 -> 3).
        XCTAssertEqual(s?.radius ?? .nan, 3, accuracy: 0.0001)
    }

    func testEncloseSinglePointHasZeroRadius() {
        let s = Scene3DMath.enclose([SIMD3(5, 5, 5)])
        XCTAssertEqual(s?.radius ?? .nan, 0, accuracy: 0.0001)
        XCTAssertEqual(s?.center.x ?? .nan, 5, accuracy: 0.0001)
    }

    func testEncloseEmptyIsNil() {
        XCTAssertNil(Scene3DMath.enclose([]))
    }

    func testCornersAreEight() {
        XCTAssertEqual(Scene3DMath.corners(min: SIMD3(0, 0, 0), max: SIMD3(1, 1, 1)).count, 8)
    }

    // Every standard view's up-vector must be perpendicular to its look direction, or
    // look(at:up:) degenerates (upside-down / gimbal). And opposite views must point
    // opposite ways.
    func testStandardViewVectorsArePerpendicular() {
        for v in Scene3DController.StandardView.allCases {
            let dot = simd.dot(v.direction, v.up)
            XCTAssertEqual(dot, 0, accuracy: 0.0001, "\(v) up not perpendicular")
            XCTAssertEqual(length(v.direction), 1, accuracy: 0.0001, "\(v) dir not unit")
        }
        XCTAssertEqual(Scene3DController.StandardView.anterior.direction,
                       -Scene3DController.StandardView.posterior.direction)
        XCTAssertEqual(Scene3DController.StandardView.left.direction,
                       -Scene3DController.StandardView.right.direction)
        XCTAssertEqual(Scene3DController.StandardView.superior.direction,
                       -Scene3DController.StandardView.inferior.direction)
    }
}
