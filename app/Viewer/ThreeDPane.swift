import SwiftUI
import SceneKit

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
    @EnvironmentObject var model: VolumeModel
    var isFocused: Bool = false
    var onToggleFocus: (() -> Void)? = nil

    var body: some View {
        VStack(spacing: 8) {
            HStack {
                Text("3D")
                    .font(.headline)
                Spacer()
                if mesh.triangleCount > 0 {
                    Button { camera(.reset) } label: {
                        Image(systemName: "arrow.counterclockwise")
                    }
                    .buttonStyle(.borderless)
                    .help("Reset / reframe 3D view")
                    Button { camera(.zoomOut) } label: {
                        Image(systemName: "minus.magnifyingglass")
                    }
                    .buttonStyle(.borderless)
                    .help("Zoom out")
                    Button { camera(.zoomIn) } label: {
                        Image(systemName: "plus.magnifyingglass")
                    }
                    .buttonStyle(.borderless)
                    .help("Zoom in")
                    Menu {
                        Button("Anterior") { camera(.anterior) }
                        Button("Posterior") { camera(.posterior) }
                        Button("Left") { camera(.left) }
                        Button("Right") { camera(.right) }
                        Button("Superior") { camera(.superior) }
                        Button("Inferior") { camera(.inferior) }
                    } label: {
                        Image(systemName: "cube")
                    }
                    .menuStyle(.borderlessButton)
                    .help("Standard anatomical views")
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

            Text("Drag to orbit • scroll or pinch to zoom • Shift-drag to pan")
                .font(.caption2)
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(12)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .strokeBorder(Color.white.opacity(0.08))
        )
        .shadow(color: .black.opacity(0.25), radius: 8, y: 3)
    }

    private func camera(_ action: MeshCameraAction) {
        mesh.cameraAction = action
        mesh.cameraActionRevision &+= 1
    }

    private var buttonTitle: String {
        if mesh.isGenerating { return "Generating…" }
        return mesh.triangleCount > 0 ? "Update 3D" : "Generate 3D"
    }

    // Cut the mask by the finished lasso, then rebuild the surface so the cut shows.
    private func performScissor(mvp: [Float], vpW: Int, vpH: Int, polygon: [Float]) {
        if seg.scissorCut(mvp: mvp, viewportWidth: vpW, viewportHeight: vpH,
                          polygon: polygon) {
            mesh.generate()
        }
    }

    private var sceneArea: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 10)
                .fill(.black)
            if !mesh.geometries.isEmpty || !markup.markups.isEmpty
                || !markup.pending.isEmpty
                || (mesh.volumeRendering && !model.volumeTexture.isEmpty) {
                MeshSceneView(geometries: mesh.geometries,
                              scissorActive: mesh.scissorActive,
                              onScissor: performScissor,
                              markups: markup.renders(),
                              pendingPoints: markup.pendingMM(),
                              pendingColor: markup.pendingColorNS(),
                              markerRadius: markup.markerRadius,
                              focusVoxel: model.focus,
                              volumeDimensions: SIMD3(model.width, model.height, model.depth),
                              spacing: model.spacing,
                              volumeRendering: mesh.volumeRendering,
                              volumeTexture: model.volumeTexture,
                              volumeTextureDimensions: model.volumeTextureDimensions,
                              volumeTextureRevision: model.volumeTextureRevision,
                              cameraAction: mesh.cameraAction,
                              cameraActionRevision: mesh.cameraActionRevision)
                    .clipShape(RoundedRectangle(cornerRadius: 10))
                    .overlay(alignment: .top) {
                        if mesh.scissorActive { ScissorBanner() }
                    }
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
