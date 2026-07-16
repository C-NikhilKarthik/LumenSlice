import XCTest
import CoreGraphics
@testable import LumenSlice

// The two-thumb RangeSlider's value<->position math (app/Shell/RangeSlider.swift).
// This is the bug-prone part of a custom control (fraction clamping, snap, bounds
// clamp), so it is pinned here the way SliceCoordinates is, without a running view.
final class RangeSliderMathTests: XCTestCase {
    // A 200pt-wide track with an 18pt thumb: thumb centres travel over [9, 191].
    private let width: CGFloat = 200
    private let thumb: CGFloat = 18
    private let bounds: ClosedRange<Float> = 0...100

    func testCenterMapsToMidValue() {
        let v = RangeSliderMath.value(atCenterX: 100, width: width, thumb: thumb,
                                      bounds: bounds, step: 1)
        XCTAssertEqual(v, 50, accuracy: 0.001)
    }

    func testTrackEndsMapToBounds() {
        let lo = RangeSliderMath.value(atCenterX: thumb / 2, width: width, thumb: thumb,
                                       bounds: bounds, step: 1)
        let hi = RangeSliderMath.value(atCenterX: width - thumb / 2, width: width,
                                       thumb: thumb, bounds: bounds, step: 1)
        XCTAssertEqual(lo, 0, accuracy: 0.001)
        XCTAssertEqual(hi, 100, accuracy: 0.001)
    }

    func testPointerOutsideTrackClampsToBounds() {
        let under = RangeSliderMath.value(atCenterX: -500, width: width, thumb: thumb,
                                          bounds: bounds, step: 1)
        let over = RangeSliderMath.value(atCenterX: 5000, width: width, thumb: thumb,
                                         bounds: bounds, step: 1)
        XCTAssertEqual(under, 0, accuracy: 0.001)
        XCTAssertEqual(over, 100, accuracy: 0.001)
    }

    func testSnapsToStep() {
        // A pointer near value 53 with step 10 snaps to 50.
        let centre = 9 + CGFloat(0.53) * (width - thumb) // fraction 0.53 of the track
        let v = RangeSliderMath.value(atCenterX: centre, width: width, thumb: thumb,
                                      bounds: bounds, step: 10)
        XCTAssertEqual(v, 50, accuracy: 0.001)
    }

    func testSnapKeepsEndsReachableForNegativeOriginBounds() {
        // Bounds not starting at zero, non-trivial step: both ends must be selectable.
        let b: ClosedRange<Float> = -1000...500
        let lo = RangeSliderMath.value(atCenterX: thumb / 2, width: width, thumb: thumb,
                                       bounds: b, step: 5)
        let hi = RangeSliderMath.value(atCenterX: width - thumb / 2, width: width,
                                       thumb: thumb, bounds: b, step: 5)
        XCTAssertEqual(lo, -1000, accuracy: 0.001)
        XCTAssertEqual(hi, 500, accuracy: 0.001)
    }

    func testLeadingXMapsValueToTrack() {
        let usable = width - thumb // 182
        XCTAssertEqual(RangeSliderMath.leadingX(for: 0, usable: usable, bounds: bounds),
                       0, accuracy: 0.001)
        XCTAssertEqual(RangeSliderMath.leadingX(for: 100, usable: usable, bounds: bounds),
                       usable, accuracy: 0.001)
        XCTAssertEqual(RangeSliderMath.leadingX(for: 50, usable: usable, bounds: bounds),
                       usable / 2, accuracy: 0.001)
    }

    func testLeadingXClampsOutOfRangeValueToTrackEnds() {
        let usable = width - thumb
        // A value above the upper bound (e.g. a Bone preset of 3000 on a narrow scan)
        // parks the thumb at the track end, not off it.
        XCTAssertEqual(RangeSliderMath.leadingX(for: 3000, usable: usable, bounds: bounds),
                       usable, accuracy: 0.001)
        XCTAssertEqual(RangeSliderMath.leadingX(for: -50, usable: usable, bounds: bounds),
                       0, accuracy: 0.001)
    }
}
