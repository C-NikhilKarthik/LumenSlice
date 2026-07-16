import SceneKit
import AppKit
import simd

// Drives the 3D scene's camera from the SwiftUI control bar. SceneKit's built-in
// camera control gives orbit/zoom/pan, but nothing to RESET once you have orbited
// away, and the model sits at its true anatomical offset (far from world origin),
// so the orbit pivot has to be re-centred on the content or the model swings wildly.
// This holds a weak reference to the live SCNView and exposes the discrete actions
// the toolbar needs: reframe, zoom, the six standard anatomical views, and snapshot.
@MainActor
final class Scene3DController: ObservableObject {
    weak var scnView: SCNView?

    enum StandardView: String, CaseIterable, Identifiable {
        case anterior, posterior, left, right, superior, inferior
        var id: String { rawValue }
        var label: String {
            switch self {
            case .anterior: return "Anterior (A)"
            case .posterior: return "Posterior (P)"
            case .left: return "Left (L)"
            case .right: return "Right (R)"
            case .superior: return "Superior (S)"
            case .inferior: return "Inferior (I)"
            }
        }
        // Gnomon axes are X = Right, Y = Anterior, Z = Superior. The camera sits on
        // the view's axis looking at the centre; `up` is chosen perpendicular to the
        // look direction so it never degenerates (Superior for A/P/L/R, Anterior for
        // the axial S/I views). Pure, so the mapping is unit-tested.
        var direction: SIMD3<Float> {
            switch self {
            case .anterior: return SIMD3(0, 1, 0)
            case .posterior: return SIMD3(0, -1, 0)
            case .right: return SIMD3(1, 0, 0)
            case .left: return SIMD3(-1, 0, 0)
            case .superior: return SIMD3(0, 0, 1)
            case .inferior: return SIMD3(0, 0, -1)
            }
        }
        var up: SIMD3<Float> {
            switch self {
            case .superior, .inferior: return SIMD3(0, 1, 0)
            default: return SIMD3(0, 0, 1)
            }
        }
    }

    func attach(_ view: SCNView) { scnView = view }

    // Mesh + markup nodes are the "content"; the camera and gnomon are excluded so
    // reframing tracks the anatomy, not the axis widget.
    private var contentNodes: [SCNNode] {
        guard let root = scnView?.scene?.rootNode else { return [] }
        return root.childNodes.filter { $0.name == "mesh" || $0.name == "markup" }
    }

    var hasContent: Bool { !contentNodes.isEmpty }

    // Reframe the camera to the content and re-centre the orbit pivot on it. This is
    // the fix for "can't get back to the model after orbiting".
    func resetView() {
        guard let view = scnView else { return }
        let nodes = contentNodes
        guard !nodes.isEmpty else { return }
        view.defaultCameraController.frameNodes(nodes)
        if let c = boundingSphere()?.center {
            view.defaultCameraController.target = SCNVector3(CGFloat(c.x), CGFloat(c.y),
                                                             CGFloat(c.z))
        }
    }

    func zoomIn() { dolly(0.8) }
    func zoomOut() { dolly(1.25) }

    // Move the camera along its line of sight toward/away from the pivot. factor < 1
    // zooms in, > 1 zooms out; distance is clamped so it can't collapse onto target.
    private func dolly(_ factor: Float) {
        guard let view = scnView, let pov = view.pointOfView,
              let sphere = boundingSphere() else { return }
        let target = sphere.center
        let p = SIMD3<Float>(Float(pov.position.x), Float(pov.position.y), Float(pov.position.z))
        let d = p - target
        let dist = max(length(d), 0.0001)
        let scale = max(dist * factor, 0.01) / dist
        let np = target + d * scale
        pov.position = SCNVector3(CGFloat(np.x), CGFloat(np.y), CGFloat(np.z))
    }

    // Snap to a standard radiological view: sit the camera on the view's axis at a
    // distance proportional to the model, looking at the centre. Setting the camera
    // controller `target` keeps a subsequent orbit pivoting on the model. (SceneKit's
    // built-in controller reads pointOfView lazily, so a manual pose can snap on the
    // very next orbit; that is cosmetic. Reset view is the sanctioned reframe.)
    func setStandardView(_ v: StandardView) {
        guard let view = scnView, let pov = view.pointOfView,
              let sphere = boundingSphere() else { return }
        let c = sphere.center
        let dist = max(sphere.radius, 1) * 3
        let eye = c + v.direction * dist
        pov.position = SCNVector3(CGFloat(eye.x), CGFloat(eye.y), CGFloat(eye.z))
        pov.look(at: SCNVector3(CGFloat(c.x), CGFloat(c.y), CGFloat(c.z)),
                 up: SCNVector3(CGFloat(v.up.x), CGFloat(v.up.y), CGFloat(v.up.z)),
                 localFront: SCNNode.localFront)
        view.defaultCameraController.target = SCNVector3(CGFloat(c.x), CGFloat(c.y), CGFloat(c.z))
    }

    // A PNG-ready snapshot of the current 3D view.
    func snapshot() -> NSImage? { scnView?.snapshot() }

    // World-space bounding sphere of the content, for framing/pivot/distance math.
    // Uses all eight corners of each node's box (Scene3DMath) so it stays correct if
    // a node ever carries a rotation.
    private func boundingSphere() -> (center: SIMD3<Float>, radius: Float)? {
        var points: [SIMD3<Float>] = []
        for n in contentNodes {
            let (lo, hi) = n.boundingBox
            let localCorners = Scene3DMath.corners(
                min: SIMD3(Float(lo.x), Float(lo.y), Float(lo.z)),
                max: SIMD3(Float(hi.x), Float(hi.y), Float(hi.z)))
            for corner in localCorners {
                let w = n.convertPosition(SCNVector3(CGFloat(corner.x), CGFloat(corner.y),
                                                     CGFloat(corner.z)), to: nil)
                points.append(SIMD3(Float(w.x), Float(w.y), Float(w.z)))
            }
        }
        return Scene3DMath.enclose(points)
    }
}
