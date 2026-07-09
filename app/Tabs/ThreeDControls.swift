import SwiftUI

// 3D-tab controls: tune marching-cubes quality and (re)generate the surface from
// the current segmentation mask. Generation runs on a background thread.
struct ThreeDControls: View {
    @EnvironmentObject var seg: SegmentationModel
    @EnvironmentObject var mesh: MeshModel

    var body: some View {
        Form {
            Section {
                Text("Build a colored 3D surface for each visible segment using "
                     + "marching cubes. Hidden or empty segments are skipped.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Section("Quality") {
                Stepper("Smoothing: \(mesh.smoothing)", value: $mesh.smoothing, in: 0...5)
                Picker("Resolution", selection: $mesh.downsample) {
                    Text("Full").tag(1)
                    Text("Half").tag(2)
                    Text("Third").tag(3)
                }
                Text("Lower resolution = fewer triangles, faster, coarser.")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }

            Section {
                Button {
                    mesh.generate()
                } label: {
                    Label(mesh.isGenerating ? "Generating…" : "Generate / Update 3D",
                          systemImage: "cube.fill")
                        .frame(maxWidth: .infinity)
                        // (per-segment colored surfaces)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .disabled(seg.voxelCount == 0 || mesh.isGenerating)

                if seg.voxelCount == 0 {
                    Text("Segment a structure first (Segment tab).")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
            }

            if mesh.triangleCount > 0 {
                Section("Mesh") {
                    LabeledContent("Triangles", value: mesh.triangleCount.formatted())
                    LabeledContent("Vertices", value: mesh.vertexCount.formatted())
                    Text("Drag to orbit, scroll to zoom.")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }

                Section("Scissor") {
                    Toggle("Scissor mode", isOn: $mesh.scissorActive)
                    Text("When on, draw a freehand loop over the surface to erase "
                         + "every voxel inside it (through the full depth), then the "
                         + "surface rebuilds. Turn off to orbit again.")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
    }
}
