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
