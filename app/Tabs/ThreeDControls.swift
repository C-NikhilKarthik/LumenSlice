import SwiftUI

// 3D-tab controls: tune marching-cubes quality and (re)generate the surface from
// the current segmentation mask. Generation runs on a background thread.
struct ThreeDControls: View {
    @EnvironmentObject var seg: SegmentationModel
    @EnvironmentObject var mesh: MeshModel

    var body: some View {
        Form {
            // Pick which segments the surface is built from, right here — toggle the
            // eye to include/exclude a segment without switching back to the Segment
            // tab (generation builds exactly the visible, non-empty ones).
            if !seg.segments.isEmpty {
                Section {
                    ForEach(seg.segments) { row in
                        SegmentListRow(row: row,
                                       isActive: row.id == seg.activeID,
                                       seg: seg)
                    }
                } header: {
                    InfoHeader("Segments",
                               help: "Toggle the eye to include or exclude a segment. "
                                   + "The surface is built from exactly the visible, "
                                   + "non-empty segments.")
                }
            }

            Section {
                Stepper("Smoothing: \(mesh.smoothing)", value: $mesh.smoothing, in: 0...5)
                Picker("Resolution", selection: $mesh.downsample) {
                    Text("Full").tag(1)
                    Text("Half").tag(2)
                    Text("Third").tag(3)
                }
            } header: {
                InfoHeader("Quality",
                           help: "Smoothing rounds the surface (0 = raw voxel steps). "
                               + "Resolution downsamples before marching cubes: lower = "
                               + "fewer triangles, faster and coarser. Use Full for "
                               + "the final export.")
            }

            Section {
                Toggle("Live-update 3D on edits", isOn: $mesh.liveUpdate3D)
            } header: {
                InfoHeader("Volume",
                           help: "Live-update rebuilds the surface after each "
                               + "committed edit and can be slow on large scans.")
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
            } header: {
                InfoHeader("3D surface",
                           help: "Build a colored 3D surface for each visible segment "
                               + "using marching cubes. Hidden or empty segments are "
                               + "skipped.")
            }

            if mesh.triangleCount > 0 {
                Section {
                    LabeledContent("Triangles", value: mesh.triangleCount.formatted())
                    LabeledContent("Vertices", value: mesh.vertexCount.formatted())
                } header: {
                    InfoHeader("Mesh",
                               help: "Blender-style controls: middle-drag to orbit, "
                                   + "Shift + middle-drag to pan, and scroll or pinch "
                                   + "to zoom.")
                }

                Section {
                    Toggle("Scissor mode", isOn: $mesh.scissorActive)
                } header: {
                    InfoHeader("Scissor",
                               help: "When on, draw a freehand loop over the surface to "
                                   + "erase every voxel inside it (through the full "
                                   + "depth), then the surface rebuilds. Turn off to "
                                   + "orbit again.")
                }
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
    }
}
