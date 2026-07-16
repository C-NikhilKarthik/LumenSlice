import SwiftUI

// The pure value<->position math for RangeSlider, factored out so it can be unit
// tested without a running view (mirrors how SliceCoordinates is tested). All of the
// bug-prone parts (fraction clamping, snap, bounds clamp) live here.
enum RangeSliderMath {
    // Leading-edge x offset of a value's thumb. The fraction is clamped to [0, 1] so
    // a value outside `bounds` parks the thumb at a track end rather than off it.
    static func leadingX(for value: Float, usable: CGFloat,
                         bounds: ClosedRange<Float>) -> CGFloat {
        let span = max(bounds.upperBound - bounds.lowerBound, 0.0001)
        let fraction = min(max((value - bounds.lowerBound) / span, 0), 1)
        return CGFloat(fraction) * usable
    }

    // The value under a pointer at track x `centre`, snapped to `step` measured FROM
    // the lower bound (so both ends stay reachable for any step) and clamped to bounds.
    static func value(atCenterX centre: CGFloat, width: CGFloat, thumb: CGFloat,
                      bounds: ClosedRange<Float>, step: Float) -> Float {
        let usable = max(width - thumb, 1)
        let clampedX = min(max(centre, thumb / 2), width - thumb / 2)
        let span = max(bounds.upperBound - bounds.lowerBound, 0.0001)
        let fraction = Float((clampedX - thumb / 2) / usable)
        let raw = bounds.lowerBound + fraction * span
        let s = max(step, 0.0001)
        let snapped = bounds.lowerBound + ((raw - bounds.lowerBound) / s).rounded() * s
        return min(max(snapped, bounds.lowerBound), bounds.upperBound)
    }
}

// A two-thumb range slider. SwiftUI/macOS ships only a single-value Slider, but a
// threshold is a WINDOW (low..high HU), so this binds both ends to one track: drag
// either handle and the highlighted band between them follows. Handles clamp so the
// low can never cross above the high (and vice versa), and both stay inside
// `bounds`. Values snap to `step`. The math lives in RangeSliderMath so it is unit
// tested; the two handles are VoiceOver-adjustable (the native Slider it replaced was
// accessible for free).
struct RangeSlider: View {
    @Binding var low: Float
    @Binding var high: Float
    let bounds: ClosedRange<Float>
    var step: Float = 1

    private let thumb: CGFloat = 18
    private let trackHeight: CGFloat = 4

    var body: some View {
        GeometryReader { geo in
            let width = geo.size.width
            let usable = max(width - thumb, 1)
            let lowX = RangeSliderMath.leadingX(for: low, usable: usable, bounds: bounds)
            let highX = RangeSliderMath.leadingX(for: high, usable: usable, bounds: bounds)

            ZStack(alignment: .leading) {
                Capsule()
                    .fill(Color.secondary.opacity(0.25))
                    .frame(height: trackHeight)
                    .padding(.horizontal, thumb / 2)

                Capsule()
                    .fill(Color.accentColor)
                    .frame(width: max(highX - lowX, 0), height: trackHeight)
                    .offset(x: lowX + thumb / 2)

                handle(isLow: true, x: lowX, width: width)
                handle(isLow: false, x: highX, width: width)
            }
            .frame(height: thumb)
            .coordinateSpace(name: Self.space)
        }
        .frame(height: thumb)
    }

    private static let space = "RangeSlider"

    @ViewBuilder
    private func handle(isLow: Bool, x: CGFloat, width: CGFloat) -> some View {
        Circle()
            .fill(Color.white)
            .overlay(Circle().strokeBorder(Color.accentColor, lineWidth: 2))
            .frame(width: thumb, height: thumb)
            .shadow(color: .black.opacity(0.2), radius: 1, y: 0.5)
            .offset(x: x)
            .gesture(drag(isLow: isLow, width: width))
            .accessibilityElement()
            .accessibilityLabel(isLow ? "Lower value" : "Upper value")
            .accessibilityValue("\(Int(isLow ? low : high))")
            .accessibilityAdjustableAction { direction in
                let delta: Float = direction == .increment ? step : -step
                if isLow { setLow(low + delta) } else { setHigh(high + delta) }
            }
    }

    // Map the pointer's x (track space) to a value, then clamp against the OTHER
    // handle so the two never cross.
    private func drag(isLow: Bool, width: CGFloat) -> some Gesture {
        DragGesture(minimumDistance: 0, coordinateSpace: .named(Self.space))
            .onChanged { g in
                let v = RangeSliderMath.value(atCenterX: g.location.x, width: width,
                                              thumb: thumb, bounds: bounds, step: step)
                if isLow { setLow(v) } else { setHigh(v) }
            }
    }

    private func setLow(_ v: Float) {
        low = min(max(v, bounds.lowerBound), high)
    }
    private func setHigh(_ v: Float) {
        high = max(min(v, bounds.upperBound), low)
    }
}
