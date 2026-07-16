import SwiftUI

// The floating control bar over the 3D view: reframe, zoom, the six standard
// anatomical views, a PNG snapshot, and (optionally) STL export. Actions are
// delegated to Scene3DController (camera) and to the host (capture / export), so
// this view is pure chrome.
struct Scene3DToolbar: View {
    @ObservedObject var controller: Scene3DController
    var onCapture: () -> Void
    var onExport: (() -> Void)? = nil

    var body: some View {
        HStack(spacing: 4) {
            iconButton("scope", "Reset view") { controller.resetView() }
            iconButton("plus.magnifyingglass", "Zoom in") { controller.zoomIn() }
            iconButton("minus.magnifyingglass", "Zoom out") { controller.zoomOut() }

            Divider().frame(height: 16)

            Menu {
                ForEach(Scene3DController.StandardView.allCases) { view in
                    Button(view.label) { controller.setStandardView(view) }
                }
            } label: {
                Image(systemName: "cube.transparent")
            }
            .menuStyle(.borderlessButton)
            .menuIndicator(.hidden)
            .frame(width: 28)
            .help("Standard views")

            Divider().frame(height: 16)

            iconButton("camera", "Save 3D screenshot (PNG)") { onCapture() }
            if let onExport {
                iconButton("square.and.arrow.up", "Export STL") { onExport() }
            }
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(.ultraThinMaterial, in: Capsule())
        .overlay(Capsule().strokeBorder(.white.opacity(0.12)))
        .shadow(color: .black.opacity(0.25), radius: 5, y: 2)
    }

    private func iconButton(_ icon: String, _ help: String,
                            _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: icon)
                .font(.system(size: 13, weight: .medium))
                .frame(width: 24, height: 22)
                .contentShape(Rectangle())
        }
        .buttonStyle(.borderless)
        .help(help)
    }
}
