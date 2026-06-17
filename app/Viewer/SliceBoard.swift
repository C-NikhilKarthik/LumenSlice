import SwiftUI

// The tri-axis slice workspace: big Axial on top, Coronal + Sagittal below.
// Extracted from ContentView during the tab-shell refactor; shared by the
// Visualize and Segment tabs.
struct SliceBoard: View {
    @EnvironmentObject var model: VolumeModel
    @Binding var dropTargeted: Bool
    // Non-nil on the Segment tab: enables the mask overlay + seed/paint gestures.
    var segment: SegmentationModel? = nil
    // When set, a single pane is maximized to fill the viewport; nil = tri-pane grid.
    @State private var focusedAxis: Int?

    var body: some View {
        VStack(spacing: 0) {
            if model.isLoading {
                VStack(spacing: 14) {
                    ProgressView()
                        .controlSize(.large)
                    Text(model.status)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if model.hasVolume {
                Group {
                    if let f = focusedAxis {
                        SlicePane(axis: f, segment: segment, isFocused: true,
                                  onToggleFocus: { focusedAxis = nil })
                            .frame(maxHeight: .infinity)
                    } else {
                        VStack(spacing: 12) {
                            SlicePane(axis: 0, segment: segment,
                                      onToggleFocus: { focusedAxis = 0 })
                                .frame(maxHeight: .infinity)
                            HStack(spacing: 12) {
                                SlicePane(axis: 1, segment: segment,
                                          onToggleFocus: { focusedAxis = 1 })
                                SlicePane(axis: 2, segment: segment,
                                          onToggleFocus: { focusedAxis = 2 })
                            }
                            .frame(maxHeight: .infinity)
                        }
                    }
                }
                .padding(14)
            } else {
                EmptyState()
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(nsColor: .underPageBackgroundColor))
        .overlay {
            if dropTargeted {
                RoundedRectangle(cornerRadius: 12)
                    .strokeBorder(Color.accentColor, style: StrokeStyle(lineWidth: 3, dash: [10]))
                    .padding(8)
            }
        }
    }
}

struct EmptyState: View {
    @EnvironmentObject var model: VolumeModel
    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: "square.stack.3d.up.slash")
                .font(.system(size: 52, weight: .thin))
                .foregroundStyle(.secondary)
            Text("No volume loaded")
                .font(.title3.weight(.medium))
            Text("Drop a DICOM folder here, or use Open Folder.")
                .foregroundStyle(.secondary)
            Button("Open DICOM Folder…") { chooseFolder(model) }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
