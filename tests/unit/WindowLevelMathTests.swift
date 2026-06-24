import XCTest
import CoreGraphics
@testable import LumenSlice

// The drag-to-adjust window/level math (app/WindowLevelDrag.swift). This is the
// fix for the hypersensitive slider, so it gets explicit coverage: direction,
// scaling, the >=1 window clamp, and identity on no movement.
final class WindowLevelMathTests: XCTestCase {
    private let base = (level: Float(40), window: Float(400))

    func testHorizontalDragWidensWindow() {
        let r = WindowLevelMath.adjusted(
            base: base, translation: CGSize(width: 50, height: 0), sensitivity: 2)
        XCTAssertEqual(r.window, 500, accuracy: 0.001) // +50 * 2
        XCTAssertEqual(r.level, 40, accuracy: 0.001)
    }

    func testDragUpRaisesLevel() {
        // SwiftUI reports a negative height when dragging up; level should rise.
        let r = WindowLevelMath.adjusted(
            base: base, translation: CGSize(width: 0, height: -30), sensitivity: 2)
        XCTAssertEqual(r.level, 100, accuracy: 0.001) // -(-30) * 2 = +60
        XCTAssertEqual(r.window, 400, accuracy: 0.001)
    }

    func testWindowClampedToAtLeastOne() {
        let r = WindowLevelMath.adjusted(
            base: (level: 0, window: 10),
            translation: CGSize(width: -1000, height: 0), sensitivity: 2)
        XCTAssertEqual(r.window, 1, accuracy: 0.001) // would go negative; clamps to 1
    }

    func testZeroDragIsIdentity() {
        let r = WindowLevelMath.adjusted(base: base, translation: .zero, sensitivity: 2)
        XCTAssertEqual(r.level, 40, accuracy: 0.001)
        XCTAssertEqual(r.window, 400, accuracy: 0.001)
    }

    func testSensitivityScalesDelta() {
        let coarse = WindowLevelMath.adjusted(
            base: base, translation: CGSize(width: 10, height: 0), sensitivity: 4)
        XCTAssertEqual(coarse.window, 440, accuracy: 0.001) // +10 * 4
    }
}
