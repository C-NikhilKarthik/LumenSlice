import SwiftUI
import SceneKit

// SceneKit viewport for the marching-cubes surfaces. Built-in camera control gives
// orbit/zoom/pan; the camera is framed to the meshes whenever they change. Each
// visible segment is its own colored node; a small R/A/S axis gnomon at the volume
// origin gives anatomical orientation.
struct MeshSceneView: NSViewRepresentable {
    var geometries: [SCNGeometry]

    final class Coordinator {
        // Identity of the geometry set we last built nodes for, so orbit/zoom (which
        // re-invoke updateNSView) don't rebuild the scene or yank the camera.
        var rendered: [ObjectIdentifier] = []
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> SCNView {
        let view = SCNView()
        let scene = SCNScene()
        view.scene = scene
        view.allowsCameraControl = true
        view.autoenablesDefaultLighting = true
        view.backgroundColor = .black
        view.antialiasingMode = .multisampling4X
        view.rendersContinuously = false

        // An explicit camera with generous clip planes: the default zNear/zFar clip
        // millimetre-scale meshes during zoom, which reads as flicker. A wide range
        // keeps the surface solid at any zoom.
        let camera = SCNCamera()
        camera.zNear = 0.1
        camera.zFar = 1_000_000
        let camNode = SCNNode()
        camNode.camera = camera
        camNode.position = SCNVector3(0, 0, 300)
        scene.rootNode.addChildNode(camNode)
        view.pointOfView = camNode
        return view
    }

    func updateNSView(_ view: SCNView, context: Context) {
        guard let root = view.scene?.rootNode else { return }
        let ids = geometries.map { ObjectIdentifier($0) }
        if ids == context.coordinator.rendered { return } // unchanged: leave camera alone
        context.coordinator.rendered = ids

        root.childNodes
            .filter { $0.name == "mesh" || $0.name == "gnomon" }
            .forEach { $0.removeFromParentNode() }
        guard !geometries.isEmpty else { return }

        let meshNodes = geometries.map { geo -> SCNNode in
            let node = SCNNode(geometry: geo)
            node.name = "mesh"
            return node
        }
        meshNodes.forEach { root.addChildNode($0) }

        // Size the gnomon to the scene so it reads at any zoom, and seat it at the
        // volume origin (a corner of the data).
        let (minB, maxB) = bounds(of: meshNodes)
        let extent = max(maxB.x - minB.x, max(maxB.y - minB.y, maxB.z - minB.z))
        let gnomon = Self.makeGnomon(length: CGFloat(max(extent, 1)) * 0.18)
        gnomon.position = minB
        root.addChildNode(gnomon)

        // Frame the camera once, only when the geometry actually changed.
        DispatchQueue.main.async {
            view.defaultCameraController.frameNodes(meshNodes)
        }
    }

    private func bounds(of nodes: [SCNNode]) -> (SCNVector3, SCNVector3) {
        var lo = SCNVector3(Float.greatestFiniteMagnitude,
                            Float.greatestFiniteMagnitude,
                            Float.greatestFiniteMagnitude)
        var hi = SCNVector3(-Float.greatestFiniteMagnitude,
                            -Float.greatestFiniteMagnitude,
                            -Float.greatestFiniteMagnitude)
        for node in nodes {
            let (nMin, nMax) = node.boundingBox // identity transform: local == world
            lo = SCNVector3(min(lo.x, nMin.x), min(lo.y, nMin.y), min(lo.z, nMin.z))
            hi = SCNVector3(max(hi.x, nMax.x), max(hi.y, nMax.y), max(hi.z, nMax.z))
        }
        if lo.x > hi.x { return (SCNVector3Zero, SCNVector3Zero) }
        return (lo, hi)
    }

    // R (X, red) / A (Y, green) / S (Z, blue) axis indicator built from cylinders.
    static func makeGnomon(length: CGFloat) -> SCNNode {
        let group = SCNNode()
        group.name = "gnomon"
        let radius = max(length * 0.04, 0.3)

        func axis(_ color: NSColor, euler: SCNVector3, offset: SCNVector3) -> SCNNode {
            let cyl = SCNCylinder(radius: radius, height: length)
            let mat = SCNMaterial()
            mat.diffuse.contents = color
            mat.emission.contents = color // visible regardless of lighting
            cyl.materials = [mat]
            let node = SCNNode(geometry: cyl)
            node.eulerAngles = euler
            node.position = offset
            return node
        }

        let half = Float(length / 2)
        // Cylinder's long axis is Y; rotate to X and Z, then push out by half-length.
        group.addChildNode(axis(.systemRed,
                                 euler: SCNVector3(0, 0, -Float.pi / 2),
                                 offset: SCNVector3(half, 0, 0)))   // R / +X
        group.addChildNode(axis(.systemGreen,
                                 euler: SCNVector3(0, 0, 0),
                                 offset: SCNVector3(0, half, 0)))   // A / +Y
        group.addChildNode(axis(.systemBlue,
                                 euler: SCNVector3(Float.pi / 2, 0, 0),
                                 offset: SCNVector3(0, 0, half)))   // S / +Z
        return group
    }
}

// The central canvas for the 3D and Export tabs: the mesh viewport, or an empty /
// generating placeholder.
struct MeshCanvas: View {
    @EnvironmentObject var mesh: MeshModel

    var body: some View {
        ZStack {
            Color.black
            if !mesh.geometries.isEmpty {
                MeshSceneView(geometries: mesh.geometries)
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
