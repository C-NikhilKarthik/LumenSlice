import SwiftUI

// The 3D surface pane that lives in the top-right of the slice workspace quad,
// alongside the three orthographic planes. It wraps the same SceneKit viewport the
// dedicated 3D tab uses (full orbit / zoom / pan camera control) and adds an inline
// Generate / Update button so you can build a surface from the current segments
// without leaving the slice view. Chrome mirrors SlicePane so it reads as a peer of
// the slice panes; a header button maximizes it to fill the viewport.
struct ThreeDPane: View {
    @EnvironmentObject var mesh: MeshModel
    @EnvironmentObject var seg: SegmentationModel
    @EnvironmentObject var markup: MarkupModel
    var isFocused: Bool = false
    var onToggleFocus: (() -> Void)? = nil

    var body: some View {
        VStack(spacing: 8) {
            HStack {
                Text("3D")
                    .font(.headline)
                Spacer()
                if mesh.triangleCount > 0 {
                    Text("\(mesh.triangleCount.formatted()) tris")
                        .font(.caption)
                        .monospacedDigit()
                        .foregroundStyle(.secondary)
                }
                if !mesh.geometries.isEmpty {
                    Button {
                        mesh.scissorActive.toggle()
                    } label: {
                        Image(systemName: "scissors")
                            .foregroundStyle(mesh.scissorActive ? Color.yellow : .secondary)
                    }
                    .buttonStyle(.borderless)
                    .help(mesh.scissorActive
                          ? "Scissor on: draw a loop to erase inside it"
                          : "Scissor: cut the surface with a lasso")
                }
                if let onToggleFocus {
                    Button(action: onToggleFocus) {
                        Image(systemName: isFocused
                              ? "arrow.down.right.and.arrow.up.left"
                              : "arrow.up.left.and.arrow.down.right")
                    }
                    .buttonStyle(.borderless)
                    .help(isFocused ? "Restore layout" : "Maximize this view")
                }
            }

            sceneArea

            // Inline generate/update — mirrors the 3D tab's primary action so the
            // surface can be (re)built from the visible segments in place.
            Button {
                mesh.generate()
            } label: {
                Label(buttonTitle, systemImage: "cube.fill")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
            .disabled(seg.voxelCount == 0 || mesh.isGenerating)
        }
        .padding(12)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .strokeBorder(Color.white.opacity(0.08))
        )
        .shadow(color: .black.opacity(0.25), radius: 8, y: 3)
    }

    private var buttonTitle: String {
        if mesh.isGenerating { return "Generating…" }
        return mesh.triangleCount > 0 ? "Update 3D" : "Generate 3D"
    }

    private var sceneArea: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 10)
                .fill(.black)
            if !mesh.geometries.isEmpty || !markup.markups.isEmpty
                || !markup.pending.isEmpty {
                // Shared viewport (surface + control bar); the export button is hidden
                // here since STL export lives on the Export tab.
                Mesh3DViewport(cornerRadius: 10, toolbarScale: 0.9)
            } else {
                VStack(spacing: 10) {
                    Image(systemName: "cube.transparent")
                        .font(.system(size: 38, weight: .thin))
                        .foregroundStyle(.secondary)
                    Text(mesh.isGenerating ? "Generating surface…" : "No 3D surface yet")
                        .font(.callout.weight(.medium))
                    if !mesh.isGenerating {
                        Text(seg.voxelCount == 0
                             ? "Segment a structure, then Generate."
                             : "Press Generate to build the surface.")
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                    }
                }
                .multilineTextAlignment(.center)
                .padding()
            }
            if mesh.isGenerating {
                ProgressView()
                    .controlSize(.large)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
