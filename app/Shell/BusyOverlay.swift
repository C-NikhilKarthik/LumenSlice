import SwiftUI

// A translucent, centered card shown over the slice canvas while a blocking
// segmentation op (fill / level trace / grow-from-seeds / auto-threshold) runs.
// It captures clicks (allowsHitTesting) so a stray click can't land mid-operation,
// and mirrors the pane card's material/border/shadow so it reads as part of the app.
struct BusyOverlay: View {
    let message: String

    var body: some View {
        ZStack {
            // A faint scrim over the canvas, and the hit-testing shield.
            Color.black.opacity(0.12)
            VStack(spacing: 12) {
                ProgressView()
                    .controlSize(.large)
                Text(message)
                    .font(.callout.weight(.medium))
                    .foregroundStyle(.secondary)
            }
            .padding(24)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
            .overlay(
                RoundedRectangle(cornerRadius: 14)
                    .strokeBorder(Color.white.opacity(0.08))
            )
            .shadow(color: .black.opacity(0.25), radius: 8, y: 3)
        }
        .allowsHitTesting(true)
    }
}

// A small non-blocking pill pinned to the top of the canvas while an intensity
// mask is active, so the user always sees that paint/fill are confined to a HU
// range. Purely informational, so it never intercepts canvas clicks.
struct MaskBadge: View {
    let range: ClosedRange<Float>

    var body: some View {
        HStack(spacing: 6) {
            Image(systemName: "paintbrush.pointed.fill")
                .font(.caption2)
            Text("Paint mask \(Int(range.lowerBound)) to \(Int(range.upperBound)) HU")
                .font(.caption.weight(.medium))
        }
        .foregroundStyle(.white)
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(Color.green.opacity(0.85), in: Capsule())
        .shadow(color: .black.opacity(0.25), radius: 4, y: 2)
        .allowsHitTesting(false)
    }
}
