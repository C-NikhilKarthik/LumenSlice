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
    // Scissor mode: when on, a freehand lasso over the 3D surface cuts the mask
    // (and camera orbit is suspended while drawing). Toggled from the 3D controls.
    @Published var scissorActive = false
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
    }

    // Export the given segment ids as one fused binary STL (their union). Returns
    // false if the selection produces no surface. Pins the handle so a concurrent
    // volume load can't free it mid-export; MUST stay synchronous (no await) so the
    // pin + !isGenerating gate keep it from tearing the one shared C++ mesh buffer
    // (the on-screen geometries are separate copies, so they are unaffected).
    @discardableResult
    func exportSTL(to url: URL, ids: [Int]) -> Bool {
        guard !isGenerating, !ids.isEmpty, let h = volume.pinHandle() else { return false }
        defer { volume.releaseHandle() }
        let ids32 = ids.map { Int32($0) }
        ids32.withUnsafeBufferPointer { buf in
            lumen_mesh_snapshot_labels(h, buf.baseAddress, Int32(buf.count))
        }
        let tris = lumen_mesh_generate(h, Int32(max(0, smoothing)), Int32(max(1, downsample)))
        guard tris > 0 else { return false } // nothing to write
        return lumen_mesh_write_stl(h, url.path) == 0
    }

    // Export each selected segment as its own STL in `directory`. Returns (written,
    // requested) so the caller can report skips. Segments that yield no surface
    // (empty, or edited away between listing and export) are skipped rather than
    // written as an empty / stale-buffer file. Same pin + gate; stays synchronous.
    func exportSTLPerSegment(into directory: URL,
                             segments: [SegmentRow]) -> (written: Int, requested: Int) {
        guard !isGenerating, !segments.isEmpty, let h = volume.pinHandle() else {
            return (0, segments.count)
        }
        defer { volume.releaseHandle() }
        var written = 0
        var used = Set<String>()
        for row in segments {
            lumen_mesh_snapshot_label(h, Int32(row.id))
            let tris = lumen_mesh_generate(h, Int32(max(0, smoothing)),
                                           Int32(max(1, downsample)))
            guard tris > 0 else { continue }
            let url = directory.appendingPathComponent(Self.stlFileName(for: row, used: &used))
            if lumen_mesh_write_stl(h, url.path) == 0 { written += 1 }
        }
        return (written, segments.count)
    }

    // A filesystem-safe, collision-free "<id>-<name>.stl". The id prefix keeps two
    // segments with the same free-text name distinct; a counter breaks any residual
    // collision so no file silently clobbers another.
    private static func stlFileName(for row: SegmentRow, used: inout Set<String>) -> String {
        var base = row.name.trimmingCharacters(in: .whitespacesAndNewlines)
        for ch in ["/", ":", "\\"] { base = base.replacingOccurrences(of: ch, with: "-") }
        if base.isEmpty { base = "segment" }
        let stem = "\(row.id)-\(base)"
        var candidate = stem
        var n = 2
        while used.contains(candidate.lowercased()) { candidate = "\(stem)-\(n)"; n += 1 }
        used.insert(candidate.lowercased())
        return "\(candidate).stl"
    }
}
