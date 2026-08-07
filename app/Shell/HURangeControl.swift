import SwiftUI

// One reusable HU-range control: a Low/High numeric pair over a two-thumb
// RangeSlider. Both HU adjustments in the app share it so they look and behave
// identically - the Segment tab's threshold window (low..high HU) and the Visualize
// tab's window/level (which is really just the visible HU window, min..max). The
// caller owns what the two ends mean via the bindings it passes: for threshold they
// are the raw low/high; for window/level they map to level = (min+max)/2 and
// window = max-min. Typed values are clamped into `bounds` and kept ordered
// (low <= high) so a stray entry can't invert the window or desync from the slider.
struct HURangeControl: View {
    @Binding var low: Float
    @Binding var high: Float
    let bounds: ClosedRange<Float>
    var step: Float = 1
    var lowLabel = "Low"
    var highLabel = "High"

    var body: some View {
        VStack(spacing: 6) {
            HStack {
                huField(lowLabel, value: Binding(
                    get: { low },
                    set: { low = min(max($0, bounds.lowerBound), high) }))
                Spacer()
                huField(highLabel, value: Binding(
                    get: { high },
                    set: { high = max(min($0, bounds.upperBound), low) }))
            }
            RangeSlider(low: $low, high: $high, bounds: bounds, step: step)
        }
    }

    // A compact "Low: [ 150 ]" numeric field; the RangeSlider above is the primary
    // control, these just allow typing an exact HU value.
    private func huField(_ title: String, value: Binding<Float>) -> some View {
        HStack(spacing: 6) {
            Text(title).foregroundStyle(.secondary)
            TextField(title, value: value,
                      format: .number.precision(.fractionLength(0)))
                .labelsHidden()
                .frame(width: 64)
                .multilineTextAlignment(.trailing)
                .textFieldStyle(.roundedBorder)
        }
    }
}
