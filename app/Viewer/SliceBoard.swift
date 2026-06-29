import SwiftUI

// The slice workspace, laid out as a 2x2 quad: Axial + 3D surface on top, Coronal
// + Sagittal below (the classic ortho-plus-3D viewer arrangement). Extracted from
// ContentView during the tab-shell refactor; shared by the Visualize and Segment
// tabs. Any pane can be maximized to fill the viewport.
struct SliceBoard: View {
    @EnvironmentObject var model: VolumeModel
    @Binding var dropTargeted: Bool
    // Non-nil on the Segment tab: enables the mask overlay + seed/paint gestures.
    var segment: SegmentationModel? = nil

    // The maximized pane, or nil for the full quad grid. A slice plane (axis 0/1/2)
    // or the 3D surface pane.
    private enum Focus: Equatable { case axis(Int); case threeD }
    @State private var focus: Focus?

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
                    if let f = focus {
                        switch f {
                        case .axis(let a):
                            SlicePane(axis: a, segment: segment, isFocused: true,
                                      onToggleFocus: { focus = nil })
                                .frame(maxHeight: .infinity)
                        case .threeD:
                            ThreeDPane(isFocused: true,
                                       onToggleFocus: { focus = nil })
                                .frame(maxHeight: .infinity)
                        }
                    } else {
                        VStack(spacing: 12) {
                            HStack(spacing: 12) {
                                SlicePane(axis: 0, segment: segment,
                                          onToggleFocus: { focus = .axis(0) })
                                ThreeDPane(onToggleFocus: { focus = .threeD })
                            }
                            .frame(maxHeight: .infinity)
                            HStack(spacing: 12) {
                                SlicePane(axis: 1, segment: segment,
                                          onToggleFocus: { focus = .axis(1) })
                                SlicePane(axis: 2, segment: segment,
                                          onToggleFocus: { focus = .axis(2) })
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
