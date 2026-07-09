import SwiftUI
import SceneKit

// SceneKit viewport for the marching-cubes surfaces. Built-in camera control gives
// orbit/zoom/pan; the camera is framed to the meshes whenever they change. Each
// visible segment is its own colored node; a small R/A/S axis gnomon at the volume
// origin gives anatomical orientation.
struct MeshSceneView: NSViewRepresentable {
    var geometries: [SCNGeometry]
    // Scissor: when active, a lasso overlay captures a freehand outline and calls
    // back with the combined view*projection matrix (16 floats, row-major), the
    // viewport size, and the outline as a flat [x0,y0,…] pixel list — everything the
    // C bridge needs to cut the mask. Inactive: events pass through to the camera.
    var scissorActive: Bool = false
    var onScissor: ((_ mvp: [Float], _ vpW: Int, _ vpH: Int, _ polygon: [Float]) -> Void)? = nil

    final class Coordinator {
        // Identity of the geometry set we last built nodes for, so orbit/zoom (which
        // re-invoke updateNSView) don't rebuild the scene or yank the camera.
        var rendered: [ObjectIdentifier] = []
        // Kept fresh each update so the overlay callback reaches the current closure.
        var parent: MeshSceneView
        weak var overlay: LassoOverlayView?

        init(_ parent: MeshSceneView) { self.parent = parent }

        // A finished lasso: read the live camera, build the row-major view*projection
        // matrix, and hand the outline to the parent's onScissor callback.
        func handleLasso(points: [CGPoint], in view: SCNView) {
            guard points.count >= 3, let pov = view.pointOfView,
                  let cam = pov.camera else { return }
            let size = view.bounds.size
            guard size.width > 0, size.height > 0 else { return }
            let proj = cam.projectionTransform(withViewportSize: size)
            let viewMat = SCNMatrix4Invert(pov.worldTransform)
            let mvp = Self.combinedRowMajor(view: viewMat, proj: proj)
            var poly = [Float](); poly.reserveCapacity(points.count * 2)
            for p in points { poly.append(Float(p.x)); poly.append(Float(p.y)) }
            parent.onScissor?(mvp, Int(size.width.rounded()),
                              Int(size.height.rounded()), poly)
        }

        // C = view · proj in row-major, row-vector convention (translation in the 4th
        // row), matching SceneKit and what scissor.hpp expects. Done in Double (macOS
        // SCNMatrix4 is CGFloat) then narrowed to Float for the bridge.
        static func combinedRowMajor(view: SCNMatrix4, proj: SCNMatrix4) -> [Float] {
            let v = flatten(view), p = flatten(proj)
            var c = [Double](repeating: 0, count: 16)
            for i in 0..<4 {
                for j in 0..<4 {
                    var s = 0.0
                    for k in 0..<4 { s += v[i * 4 + k] * p[k * 4 + j] }
                    c[i * 4 + j] = s
                }
            }
            return c.map { Float($0) }
        }

        static func flatten(_ m: SCNMatrix4) -> [Double] {
            [Double(m.m11), Double(m.m12), Double(m.m13), Double(m.m14),
             Double(m.m21), Double(m.m22), Double(m.m23), Double(m.m24),
             Double(m.m31), Double(m.m32), Double(m.m33), Double(m.m34),
             Double(m.m41), Double(m.m42), Double(m.m43), Double(m.m44)]
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator(self) }

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

        // Transparent lasso layer on top of the SCNView. When inactive its hitTest
        // returns nil, so orbit/zoom go straight to the camera; when active it
        // captures the drag and reports the finished outline.
        let overlay = LassoOverlayView(frame: view.bounds)
        overlay.autoresizingMask = [.width, .height]
        overlay.onFinish = { [weak view] points in
            guard let view else { return }
            context.coordinator.handleLasso(points: points, in: view)
        }
        view.addSubview(overlay)
        context.coordinator.overlay = overlay
        return view
    }

    func updateNSView(_ view: SCNView, context: Context) {
        context.coordinator.parent = self
        context.coordinator.overlay?.active = scissorActive
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

// Transparent freehand-lasso layer sat on top of the SCNView. Flipped so its
// coordinates are top-left origin / y-down (the space scissor.hpp maps NDC into).
// When inactive, hitTest returns nil so mouse events reach the SCNView's camera
// control; when active it records the drag, draws the outline, and reports it.
final class LassoOverlayView: NSView {
    var active = false
    var onFinish: (([CGPoint]) -> Void)?
    private var points: [CGPoint] = []

    override var isFlipped: Bool { true }

    override func hitTest(_ point: NSPoint) -> NSView? { active ? self : nil }

    override func mouseDown(with event: NSEvent) {
        guard active else { return }
        points = [convert(event.locationInWindow, from: nil)]
        needsDisplay = true
    }

    override func mouseDragged(with event: NSEvent) {
        guard active else { return }
        points.append(convert(event.locationInWindow, from: nil))
        needsDisplay = true
    }

    override func mouseUp(with event: NSEvent) {
        guard active else { return }
        let finished = points
        points = []
        needsDisplay = true
        if finished.count >= 3 { onFinish?(finished) }
    }

    override func draw(_ dirtyRect: NSRect) {
        guard active, points.count > 1 else { return }
        let path = NSBezierPath()
        path.move(to: points[0])
        for p in points.dropFirst() { path.line(to: p) }
        path.close()
        NSColor.systemYellow.withAlphaComponent(0.12).setFill()
        path.fill()
        NSColor.systemYellow.setStroke()
        path.lineWidth = 1.5
        path.stroke()
    }
}

// The central canvas for the 3D and Export tabs: the mesh viewport, or an empty /
// generating placeholder.
struct MeshCanvas: View {
    @EnvironmentObject var mesh: MeshModel
    @EnvironmentObject var seg: SegmentationModel

    var body: some View {
        ZStack {
            Color.black
            if !mesh.geometries.isEmpty {
                MeshSceneView(geometries: mesh.geometries,
                              scissorActive: mesh.scissorActive,
                              onScissor: performScissor)
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
        .overlay(alignment: .top) {
            if mesh.scissorActive {
                ScissorBanner()
            }
        }
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

    // Cut the mask by the finished lasso, then rebuild the surface so the cut shows.
    private func performScissor(mvp: [Float], vpW: Int, vpH: Int, polygon: [Float]) {
        if seg.scissorCut(mvp: mvp, viewportWidth: vpW, viewportHeight: vpH,
                          polygon: polygon) {
            mesh.generate()
        }
    }
}

// The hint shown while scissor mode is on, so it's obvious the drag now cuts.
struct ScissorBanner: View {
    var body: some View {
        Label("Scissor: draw a loop over the surface to erase inside it",
              systemImage: "scissors")
            .font(.caption)
            .padding(.horizontal, 10).padding(.vertical, 5)
            .background(.yellow.opacity(0.85), in: Capsule())
            .foregroundStyle(.black)
            .padding(10)
    }
}
