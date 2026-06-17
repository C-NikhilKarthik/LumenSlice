import XCTest
import CoreGraphics
@testable import LumenSlice

// Tap -> image-pixel mapping (app/Viewer/SliceCoordinates.swift). This is the
// Swift half of the seed/paint coordinate path the eng review flagged as a
// silent-failure risk: a wrong mapping picks the wrong voxel with no error. The
// asserts pin the letterbox math for square and non-square display aspects.
final class SliceCoordinatesTests: XCTestCase {
    // 216x216 pane, padding 8 -> a 200x200 available area.
    private let container = CGSize(width: 216, height: 216)

    func testCenterMapsToCenterPixelSquare() {
        let p = SliceCoordinates.pixel(
            forTap: CGPoint(x: 108, y: 108), container: container,
            imageWidth: 128, imageHeight: 128, aspect: 1.0)
        XCTAssertEqual(p?.x, 64)
        XCTAssertEqual(p?.y, 64)
    }

    func testImageCornersMapToPixelExtremes() {
        // Top-left of the fitted image (origin = padding for a square fit).
        let tl = SliceCoordinates.pixel(
            forTap: CGPoint(x: 8, y: 8), container: container,
            imageWidth: 128, imageHeight: 128, aspect: 1.0)
        XCTAssertEqual(tl?.x, 0)
        XCTAssertEqual(tl?.y, 0)

        // Bottom-right, just inside the image edge.
        let br = SliceCoordinates.pixel(
            forTap: CGPoint(x: 207.9, y: 207.9), container: container,
            imageWidth: 128, imageHeight: 128, aspect: 1.0)
        XCTAssertEqual(br?.x, 127)
        XCTAssertEqual(br?.y, 127)
    }

    func testTapInLetterboxMarginReturnsNil() {
        // aspect 2.0 -> image is 200x100, vertically centered: rows 58..158 used,
        // so y=20 is in the top margin and must not map to a voxel.
        let p = SliceCoordinates.pixel(
            forTap: CGPoint(x: 108, y: 20), container: container,
            imageWidth: 128, imageHeight: 64, aspect: 2.0)
        XCTAssertNil(p)
    }

    func testNonSquareAspectCentre() {
        // aspect 2.0, image 128x64. Center of the pane is the center of the fitted
        // image -> middle pixel.
        let p = SliceCoordinates.pixel(
            forTap: CGPoint(x: 108, y: 108), container: container,
            imageWidth: 128, imageHeight: 64, aspect: 2.0)
        XCTAssertEqual(p?.x, 64)
        XCTAssertEqual(p?.y, 32)
    }

    func testTapOutsideContainerReturnsNil() {
        let p = SliceCoordinates.pixel(
            forTap: CGPoint(x: -5, y: -5), container: container,
            imageWidth: 128, imageHeight: 128, aspect: 1.0)
        XCTAssertNil(p)
    }
}
