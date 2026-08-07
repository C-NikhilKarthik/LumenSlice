import SwiftUI
import AppKit
import UniformTypeIdentifiers

// Root window. A standard opaque titlebar (traffic lights + toolbar + separator
// line) sits across the top; EVERYTHING else - the icon rail, the collapsible
// control panel, and the canvas - sits BELOW it as a distinct row, so the panel
// reads as its own rectangle starting under the navbar rather than flowing up
// behind it. The panel is a fixed width sized to fit its widest tab, so it never
// spills over the rail. The toolbar's sidebar button collapses/expands the panel.
//
//   ┌──────────── titlebar: ⊙⊙⊙  ⊟  LumenSlice · tab      Open Folder ─────┐
//   ├──────────────────────────── separator ────────────────────────────┤
//   │ rail │  control panel   │   canvas (adapts per tab)                │
//   └──────┴──────────────────┴──────────────────────────────────────────┘
struct AppShell: View {
    @EnvironmentObject var model: VolumeModel
    @EnvironmentObject var segmentation: SegmentationModel
    @EnvironmentObject var mesh: MeshModel
    @EnvironmentObject var markup: MarkupModel
    // Owned by the App so the global Undo command can route by active tab.
    @Binding var selectedTab: WorkspaceTab
    @State private var dropTargeted = false
    // The features panel collapses to give the canvas the full width.
    @State private var panelCollapsed = false

    // Fixed panel width so every tab's control panel has the same breadth. Sized to
    // fit the widest tab (Segment - its 5-item tool picker + threshold fields), so no
    // control is clipped on the right.
    private let panelWidth: CGFloat = 360

    var body: some View {
        HStack(spacing: 0) {
            TabRail(selection: $selectedTab)
                .onChange(of: selectedTab) { _, newTab in
                    // Only the Segment tab does overlay extraction work.
                    segmentation.isActive = (newTab == .segment)
                    // Leaving the Markups tab exits placement, so slice clicks go
                    // back to navigation / painting instead of dropping points.
                    if newTab != .markups { markup.placing = false }
                }
            if !panelCollapsed {
                controlPanel
                    .frame(width: panelWidth)
                Divider()
            }
            canvas
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .onDrop(of: [.fileURL], isTargeted: $dropTargeted) { providers in
                    handleDrop(providers, model)
                }
        }
        .background(WindowAccessor())
        .toolbar {
            ToolbarItem(placement: .navigation) {
                Button {
                    withAnimation(.easeInOut(duration: 0.15)) { panelCollapsed.toggle() }
                } label: {
                    Image(systemName: "sidebar.left")
                }
                .help(panelCollapsed ? "Show features panel" : "Hide features panel")
                .accessibilityLabel(panelCollapsed ? "Show features panel"
                                                   : "Hide features panel")
            }
            ToolbarItem(placement: .principal) {
                HStack(spacing: 6) {
                    Text("LumenSlice").font(.headline)
                    Text(selectedTab.title)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
            }
            ToolbarItem(placement: .primaryAction) {
                Button {
                    chooseFolder(model)
                } label: {
                    Label("Open Folder", systemImage: "folder.badge.plus")
                }
            }
        }
        .toolbarBackground(.visible, for: .windowToolbar)
    }

    // The left control panel: just the active tab's controls (identity lives in the
    // titlebar). Fixed width via the caller so all tabs match.
    @ViewBuilder private var controlPanel: some View {
        Group {
            switch selectedTab {
            case .visualize: VisualizeControls()
            case .segment:   SegmentControls()
            case .markups:   MarkupControls()
            case .threeD:    ThreeDControls()
            case .export:    ExportControls()
            }
        }
        .background(
            VisualEffectView(material: .sidebar, blendingMode: .behindWindow)
        )
    }

    // The central canvas, adapted to the active tab.
    @ViewBuilder private var canvas: some View {
        switch selectedTab {
        case .visualize:
            SliceBoard(dropTargeted: $dropTargeted)
        case .segment:
            SliceBoard(dropTargeted: $dropTargeted, segment: segmentation)
        case .markups:
            SliceBoard(dropTargeted: $dropTargeted)
        case .threeD, .export:
            MeshCanvas()
        }
    }
}
