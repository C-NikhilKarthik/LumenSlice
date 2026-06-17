import SwiftUI
import SceneKit

// SceneKit viewport for the marching-cubes surface. Built-in camera control gives
// orbit/zoom/pan; the camera is framed to the mesh whenever it changes.
struct MeshSceneView: NSViewRepresentable {
    var geometry: SCNGeometry?

    func makeNSView(context: Context) -> SCNView {
        let view = SCNView()
        view.scene = SCNScene()
        view.allowsCameraControl = true
        view.autoenablesDefaultLighting = true
        view.backgroundColor = .black
        view.antialiasingMode = .multisampling4X
        return view
    }

    func updateNSView(_ view: SCNView, context: Context) {
        guard let root = view.scene?.rootNode else { return }
        root.childNodes.filter { $0.name == "mesh" }.forEach { $0.removeFromParentNode() }
        guard let geometry else { return }
        let node = SCNNode(geometry: geometry)
        node.name = "mesh"
        root.addChildNode(node)
        // Frame the camera to the new geometry (deferred so bounds are ready).
        DispatchQueue.main.async {
            view.defaultCameraController.frameNodes([node])
        }
    }
}

// The central canvas for the 3D and Export tabs: the mesh viewport, or an empty /
// generating placeholder.
struct MeshCanvas: View {
    @EnvironmentObject var mesh: MeshModel

    var body: some View {
        ZStack {
            Color.black
            if mesh.geometry != nil {
                MeshSceneView(geometry: mesh.geometry)
            } else {
                VStack(spacing: 12) {
                    Image(systemName: "cube.transparent")
                        .font(.system(size: 52, weight: .thin))
                        .foregroundStyle(.secondary)
                    Text(mesh.isGenerating ? "Generating surface…" : "No 3D surface yet")
                        .font(.title3.weight(.medium))
                    if !mesh.isGenerating {
                        Text("Segment a structure, then press Generate in the 3D tab.")
                            .foregroundStyle(.secondary)
                    }
                }
            }
            if mesh.isGenerating {
                ProgressView()
                    .controlSize(.large)
                    .padding(.top, 120)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .overlay(alignment: .bottomTrailing) {
            if mesh.triangleCount > 0 {
                Text("\(mesh.triangleCount.formatted()) tris")
                    .font(.caption2.monospacedDigit())
                    .padding(.horizontal, 7).padding(.vertical, 3)
                    .background(.black.opacity(0.5), in: Capsule())
                    .foregroundStyle(.white.opacity(0.9))
                    .padding(12)
            }
        }
    }
}
