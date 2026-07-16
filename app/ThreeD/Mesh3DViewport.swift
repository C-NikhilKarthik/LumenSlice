import SwiftUI

// The shared 3D viewport: the SceneKit surface plus the floating control bar and its
// scissor / screenshot / export wiring. Both the 3D-tab canvas (MeshCanvas) and the
// slice-workspace quadrant (ThreeDPane) embed this, so the camera controller and the
// toolbar behaviour live in ONE place instead of being hand-duplicated in two views
// that would drift. The host owns the placeholder / generating / badge chrome.
struct Mesh3DViewport: View {
    @EnvironmentObject var mesh: MeshModel
    @EnvironmentObject var seg: SegmentationModel
    @EnvironmentObject var markup: MarkupModel
    @StateObject private var camera = Scene3DController()
    var cornerRadius: CGFloat = 0
    var toolbarScale: CGFloat = 1
    var showExport = false

    var body: some View {
        MeshSceneView(geometries: mesh.geometries,
                      scissorActive: mesh.scissorActive,
                      onScissor: performScissor,
                      markups: markup.renders(),
                      pendingPoints: markup.pendingMM(),
                      pendingColor: markup.pendingColorNS(),
                      markerRadius: markup.markerRadius,
                      controller: camera)
            .clipShape(RoundedRectangle(cornerRadius: cornerRadius))
            .overlay(alignment: .top) {
                if mesh.scissorActive { ScissorBanner() }
            }
            .overlay(alignment: .bottom) {
                Scene3DToolbar(controller: camera, onCapture: captureScreenshot,
                               onExport: (showExport && mesh.triangleCount > 0) ? exportSTL : nil)
                    .scaleEffect(toolbarScale)
                    .padding(.bottom, 10)
            }
    }

    // Cut the mask by the finished lasso, then rebuild the surface so the cut shows.
    private func performScissor(mvp: [Float], vpW: Int, vpH: Int, polygon: [Float]) {
        if seg.scissorCut(mvp: mvp, viewportWidth: vpW, viewportHeight: vpH,
                          polygon: polygon) {
            mesh.generate()
        }
    }

    private func captureScreenshot() {
        guard let image = camera.snapshot(),
              let url = SceneExport.savePanel(name: "LumenSlice-3D.png", type: .png)
        else { return }
        _ = SceneExport.writePNG(image, to: url)
    }

    // Quick export of the VISIBLE, non-empty segments fused into one STL, so the
    // toolbar matches what the Export tab shows by default (per-segment selection
    // lives on the Export tab).
    private func exportSTL() {
        let ids = seg.segments.filter { $0.visible && $0.voxels > 0 }.map { $0.id }
        guard !ids.isEmpty,
              let url = SceneExport.savePanel(name: "LumenSlice.stl", type: .stl) else { return }
        mesh.exportSTL(to: url, ids: ids)
    }
}
