import SwiftUI

// The central canvas for the 3D and Export tabs: the shared 3D viewport (surface +
// control bar) when there is content, or an empty / generating placeholder. The
// viewport owns the camera controller and the scissor / capture / export wiring.
struct MeshCanvas: View {
    @EnvironmentObject var mesh: MeshModel
    @EnvironmentObject var markup: MarkupModel

    private var hasContent: Bool {
        !mesh.geometries.isEmpty || !markup.markups.isEmpty || !markup.pending.isEmpty
    }

    var body: some View {
        ZStack {
            Color.black
            if hasContent {
                Mesh3DViewport(showExport: true)
            } else {
                placeholder
            }
            if mesh.isGenerating {
                ProgressView()
                    .controlSize(.large)
                    .padding(.top, 120)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .overlay(alignment: .bottomTrailing) {
            if mesh.triangleCount > 0 {
                Text("\(mesh.triangleCount.formatted()) tris")
                    .font(.caption2.monospacedDigit())
                    .padding(.horizontal, 7).padding(.vertical, 3)
                    .background(.black.opacity(0.5), in: Capsule())
                    .foregroundStyle(.white.opacity(0.9))
                    .padding(12)
            }
        }
    }

    private var placeholder: some View {
        VStack(spacing: 12) {
            Image(systemName: "cube.transparent")
                .font(.system(size: 52, weight: .thin))
                .foregroundStyle(.secondary)
            Text(mesh.isGenerating ? "Generating surface…" : "No 3D surface yet")
                .font(.title3.weight(.medium))
            if !mesh.isGenerating {
                Text("Segment a structure, then press Generate in the 3D tab.")
                    .foregroundStyle(.secondary)
            }
        }
    }
}

// The hint shown while scissor mode is on, so it's obvious the drag now cuts.
struct ScissorBanner: View {
    var body: some View {
        Label("Scissor: draw a loop over the surface to erase inside it",
              systemImage: "scissors")
            .font(.caption)
            .padding(.horizontal, 10).padding(.vertical, 5)
            .background(.yellow.opacity(0.85), in: Capsule())
            .foregroundStyle(.black)
            .padding(10)
    }
}
