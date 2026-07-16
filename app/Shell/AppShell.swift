import SwiftUI
import AppKit
import UniformTypeIdentifiers

// Root window: a left icon rail (Visualize / Segment / 3D / Export) + a per-tab
// control panel + an adaptive central canvas. Selecting a tab swaps ONLY the
// control panel and the canvas; the other tabs' controls hide.
//
//   ┌──────┬──────────────────┬───────────────────────────┐
//   │ rail │  control panel   │   canvas (adapts per tab)  │
//   │  V   │  (active tab's   │   Visualize/Segment -> 3   │
//   │  S   │   buttons only)  │     slice panes            │
//   │  3D  │                  │   3D -> SceneKit mesh      │
//   │  E   │                  │   Export -> preview        │
//   └──────┴──────────────────┴───────────────────────────┘
//
// P0 wires the Visualize tab fully (identical to the pre-refactor app); the
// Segment / 3D / Export tabs are placeholders filled in by later phases.
struct AppShell: View {
    @EnvironmentObject var model: VolumeModel
    @EnvironmentObject var segmentation: SegmentationModel
    @EnvironmentObject var mesh: MeshModel
    @EnvironmentObject var markup: MarkupModel
    // Owned by the App so the global Undo command can route by active tab.
    @Binding var selectedTab: WorkspaceTab
    @State private var dropTargeted = false

    var body: some View {
        HStack(spacing: 0) {
            TabRail(selection: $selectedTab)
                .onChange(of: selectedTab) { newTab in
                    // Only the Segment tab does overlay extraction work.
                    segmentation.isActive = (newTab == .segment)
                    // Leaving the Markups tab exits placement, so slice clicks go
                    // back to navigation / painting instead of dropping points.
                    if newTab != .markups { markup.placing = false }
                }
            NavigationSplitView {
                controlPanel
                    .navigationSplitViewColumnWidth(min: 262, ideal: 292, max: 360)
            } detail: {
                canvas
                    .navigationTitle("LumenSlice")
                    .toolbar {
                        ToolbarItem(placement: .primaryAction) {
                            Button {
                                chooseFolder(model)
                            } label: {
                                Label("Open Folder", systemImage: "folder.badge.plus")
                            }
                        }
                    }
                    .toolbarBackground(.hidden, for: .windowToolbar)
                    .onDrop(of: [.fileURL], isTargeted: $dropTargeted) { providers in
                        handleDrop(providers, model)
                    }
            }
        }
        .background(WindowAccessor())
    }

    // The left control panel: a shared app header + the active tab's controls.
    @ViewBuilder private var controlPanel: some View {
        VStack(spacing: 0) {
            AppHeader(tab: selectedTab)
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
                .ignoresSafeArea()
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

// MARK: - Header + placeholders

private struct AppHeader: View {
    let tab: WorkspaceTab
    var body: some View {
        HStack(spacing: 11) {
            Image(systemName: "square.stack.3d.up.fill")
                .font(.system(size: 22, weight: .semibold))
                .foregroundStyle(.tint)
            VStack(alignment: .leading, spacing: 1) {
                Text("LumenSlice")
                    .font(.title3.weight(.semibold))
                Text(tab.title)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
        }
        .padding(.horizontal, 18)
        .padding(.top, 24)
        .padding(.bottom, 10)
    }
}

