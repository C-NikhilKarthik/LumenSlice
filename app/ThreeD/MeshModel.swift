import SwiftUI
import Combine
import SceneKit
import LumenCore

enum MeshCameraAction: Equatable {
    case reset, zoomIn, zoomOut
    case anterior, posterior, left, right, superior, inferior
}

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
    private var automaticGenerateTask: Task<Void, Never>?
    private var automaticGeneratePending = false

    @Published var smoothing: Int = 1
    @Published var downsample: Int = 1
    @Published var liveUpdate3D = UserDefaults.standard.object(
        forKey: "liveUpdate3D") as? Bool ?? false {
        didSet { UserDefaults.standard.set(liveUpdate3D, forKey: "liveUpdate3D") }
    }
    // Scissor mode: when on, a freehand lasso over the 3D surface cuts the mask
    // (and camera orbit is suspended while drawing). Toggled from the 3D controls.
    @Published var scissorActive = false
    @Published private(set) var isGenerating = false
    @Published private(set) var triangleCount = 0
    @Published private(set) var vertexCount = 0
    @Published private(set) var geometries: [SCNGeometry] = []
    @Published var cameraAction: MeshCameraAction? = nil
    @Published var cameraActionRevision = 0

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

        // Only committed segmentation actions invalidate the 3D surface.
        // Threshold slider previews and mask-source previews remain 2D-only.
        segmentation.$meshRevision
            .dropFirst()
            .sink { [weak self] _ in
                guard let self, self.liveUpdate3D else { return }
                self.scheduleAutomaticGenerate()
            }
            .store(in: &cancellables)

        // A grow-from-seeds preview is a deliberate "show me the 3D result" step, so
        // the surface is always (re)built when one activates - independent of the
        // live-update opt-in that governs incremental edits. Deferred to the next
        // runloop tick so the segment mirror has refreshed before generate() reads it.
        segmentation.$growPreviewActive
            .dropFirst()
            .sink { [weak self] active in
                guard let self, active else { return }
                DispatchQueue.main.async { self.generate() }
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
        automaticGeneratePending = false
        automaticGenerateTask?.cancel()
        guard volume.handle != nil, !isGenerating else { return }
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

        // Pin the handle so loading a new volume mid-generation defers the free
        // rather than pulling the buffer out from under the background march
        // (use-after-free). Released in finishGenerate on every path.
        guard let pinned = volume.pinHandle() else { return }
        isGenerating = true
        let bits = UInt(bitPattern: pinned)
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
            await MainActor.run { self.finishGenerate(collected, from: bits) }
        }
    }

    private func finishGenerate(_ built: [Built], from bits: UInt) {
        defer { isGenerating = false; volume.releaseHandle() }
        // If the volume was swapped while we generated, the mesh belongs to a
        // now-replaced handle — discard it rather than show it over the new volume.
        guard OpaquePointer(bitPattern: bits) == volume.handle else { return }
        geometries = built.map { $0.geometry }
        triangleCount = built.reduce(0) { $0 + $1.triangles }
        vertexCount = built.reduce(0) { $0 + $1.vertices }
        if automaticGeneratePending { scheduleAutomaticGenerate() }
    }

    private func scheduleAutomaticGenerate() {
        guard volume.handle != nil else { return }
        automaticGeneratePending = true
        automaticGenerateTask?.cancel()
        automaticGenerateTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 180_000_000)
            guard !Task.isCancelled, let self else { return }
            if self.isGenerating {
                return // finishGenerate retries when the current job completes.
            }
            self.automaticGeneratePending = false
            self.generate()
        }
    }

    // Export the union of all segments as one binary STL (regenerates the combined
    // surface into the buffer; the on-screen per-segment geometries are unaffected).
    // Refuses to run while a background generate is in flight — both write the one
    // shared C++ mesh buffer, so overlapping them would tear the STL.
    // Export the chosen segments (their union) as one binary STL: snapshot the
    // selected labels, regenerate the shared buffer, write. Empty ids is a no-op
    // failure the caller messages. Refuses to run while a background generate is in
    // flight — both write the one shared C++ mesh buffer, so overlapping them would
    // tear the STL.
    func exportSTL(to url: URL, ids: [Int]) -> Bool {
        guard let h = volume.handle, !isGenerating, !ids.isEmpty else { return false }
        let ids32 = ids.map { Int32($0) }
        ids32.withUnsafeBufferPointer { buf in
            lumen_mesh_snapshot_labels(h, buf.baseAddress, Int32(buf.count))
        }
        let tris = lumen_mesh_generate(h, Int32(max(0, smoothing)), Int32(max(1, downsample)))
        // Refuse to write a header-only STL: if marching cubes produced no triangles
        // there is nothing to export, so report failure rather than leave the user
        // with an 84-byte file that opens as "no mesh".
        guard tris > 0 else { return false }
        return lumen_mesh_write_stl(h, url.path) == 0
    }
}
