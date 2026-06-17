import SwiftUI
import Combine
import SceneKit
import LumenCore

// Drives marching-cubes generation and holds one colored SCNGeometry per visible
// segment. Follows the eng review's "snapshot mask, compute off-handle" decision:
// each segment's mask is binarized + snapshotted on the main thread, marching cubes
// runs on a background task reading only that snapshot, and the finished mesh is
// read back on the main actor — so generation never races the live mask the user
// keeps editing. Segments are processed sequentially because they share the one
// mesh buffer in the C++ handle.
@MainActor
final class MeshModel: ObservableObject {
    private let volume: VolumeModel
    private let segmentation: SegmentationModel
    private var cancellables = Set<AnyCancellable>()

    @Published var smoothing: Int = 1
    @Published var downsample: Int = 1
    @Published private(set) var isGenerating = false
    @Published private(set) var triangleCount = 0
    @Published private(set) var vertexCount = 0
    @Published private(set) var geometries: [SCNGeometry] = []

    init(volume: VolumeModel, segmentation: SegmentationModel) {
        self.volume = volume
        self.segmentation = segmentation
        // A new volume invalidates any existing surfaces.
        volume.$hasVolume
            .sink { [weak self] _ in
                self?.geometries = []
                self?.triangleCount = 0
                self?.vertexCount = 0
            }
            .store(in: &cancellables)
    }

    private struct SegmentSpec {
        let id: Int32
        let r: CGFloat, g: CGFloat, b: CGFloat
    }
    private struct Built {
        let geometry: SCNGeometry
        let triangles: Int
        let vertices: Int
    }

    func generate() {
        guard let h = volume.handle, !isGenerating else { return }
        // Capture the visible, non-empty segments (id + colour components) up front
        // on the main actor; only Sendable value types cross into the task.
        let specs: [SegmentSpec] = segmentation.segments
            .filter { $0.visible && $0.voxels > 0 }
            .map { row in
                let ns = NSColor(row.color).usingColorSpace(.sRGB) ?? .gray
                return SegmentSpec(id: Int32(row.id), r: ns.redComponent,
                                   g: ns.greenComponent, b: ns.blueComponent)
            }
        guard !specs.isEmpty else {
            geometries = []; triangleCount = 0; vertexCount = 0
            return
        }

        isGenerating = true
        let bits = UInt(bitPattern: h)
        let smooth = Int32(max(0, smoothing))
        let ds = Int32(max(1, downsample))

        Task.detached(priority: .userInitiated) {
            var built: [Built] = []
            for spec in specs {
                // 1. main: freeze this segment's voxels into the snapshot.
                await MainActor.run {
                    if let handle = OpaquePointer(bitPattern: bits) {
                        lumen_mesh_snapshot_label(handle, spec.id)
                    }
                }
                // 2. background: march the snapshot.
                guard let handle = OpaquePointer(bitPattern: bits) else { break }
                let tris = Int(lumen_mesh_generate(handle, smooth, ds))
                guard tris > 0 else { continue }
                // 3. main: copy the buffers into a colored SCNGeometry before the
                //    next segment overwrites them.
                let result: Built? = await MainActor.run {
                    guard let hh = OpaquePointer(bitPattern: bits) else { return nil }
                    let color = NSColor(srgbRed: spec.r, green: spec.g, blue: spec.b,
                                        alpha: 1)
                    guard let geo = MeshBuilder.geometry(from: hh, color: color) else {
                        return nil
                    }
                    return Built(geometry: geo, triangles: tris,
                                 vertices: Int(lumen_mesh_vertex_count(hh)))
                }
                if let result { built.append(result) }
            }
            let collected = built // immutable copy for the cross-actor hop
            await MainActor.run { self.finishGenerate(collected) }
        }
    }

    private func finishGenerate(_ built: [Built]) {
        geometries = built.map { $0.geometry }
        triangleCount = built.reduce(0) { $0 + $1.triangles }
        vertexCount = built.reduce(0) { $0 + $1.vertices }
        isGenerating = false
    }

    // Export the union of all segments as one binary STL (regenerates the combined
    // surface into the buffer; the on-screen per-segment geometries are unaffected).
    func exportSTL(to url: URL) -> Bool {
        guard let h = volume.handle else { return false }
        lumen_mesh_snapshot(h) // every labelled voxel
        _ = lumen_mesh_generate(h, Int32(max(0, smoothing)), Int32(max(1, downsample)))
        return lumen_mesh_write_stl(h, url.path) == 0
    }
}
