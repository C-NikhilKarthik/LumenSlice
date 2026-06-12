import SwiftUI
import CoreGraphics

// Pure window/level drag math, factored out of the gesture so it is unit-testable
// without SwiftUI. Horizontal drag changes the window (contrast), vertical drag
// changes the level (brightness). Window is clamped to >= 1 HU (a zero-width
// window divides by zero in the C++ extractor).
enum WindowLevelMath {
    static func adjusted(base: (level: Float, window: Float),
                         translation: CGSize,
                         sensitivity: Float) -> (level: Float, window: Float) {
        let deltaWindow = Float(translation.width) * sensitivity
        let deltaLevel = Float(-translation.height) * sensitivity
        return (level: base.level + deltaLevel,
                window: max(1, base.window + deltaWindow))
    }
}

// Radiology-standard window/level control: left-drag on a slice adjusts the
// transfer function live - horizontal = window (contrast), vertical = level
// (brightness). This is the primary W/L control; the sliders/fields in the
// sidebar are for coarse sweeps and exact entry.
//
//   drag right  -> wider window (lower contrast)
//   drag left   -> narrower window (higher contrast)
//   drag up     -> higher level  (brighter)
//   drag down   -> lower level   (darker)
//
// The gesture captures W/L at drag start and applies a delta from there, so it
// never accumulates rounding drift across a single drag. Sensitivity is a fixed
// HU-per-point: the old slider mapped a ~4000 HU window across the whole track,
// which is exactly what felt twitchy. 2 HU/point feels precise.
struct WindowLevelDrag: ViewModifier {
    @ObservedObject var model: VolumeModel

    private static let sensitivity: Float = 2.0

    @State private var start: (level: Float, window: Float)?

    func body(content: Content) -> some View {
        content.gesture(
            DragGesture(minimumDistance: 1)
                .onChanged { value in
                    let base = start ?? (model.level, model.window)
                    if start == nil { start = base }
                    let result = WindowLevelMath.adjusted(
                        base: base, translation: value.translation,
                        sensitivity: Self.sensitivity)
                    model.setWindowLevel(level: result.level, window: result.window)
                }
                .onEnded { _ in start = nil }
        )
    }
}

extension View {
    // Attach drag-to-adjust window/level (horizontal = window, vertical = level).
    func windowLevelDrag(_ model: VolumeModel) -> some View {
        modifier(WindowLevelDrag(model: model))
    }
}
