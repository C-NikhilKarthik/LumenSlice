import SwiftUI
import Combine
import SceneKit
import LumenCore

// Drives marching-cubes generation and holds the resulting SCNGeometry. Follows
// the eng review's "snapshot mask, compute off-handle" decision: the mask is
// snapshotted on the main thread, marching cubes runs on a background task
// reading only that snapshot, and the finished mesh is read back on the main
// actor — so generation never races the live mask the user keeps editing.
@MainActor
final class MeshModel: ObservableObject {
    private let volume: VolumeModel
    private var cancellables = Set<AnyCancellable>()

    @Published var smoothing: Int = 1
    @Published var downsample: Int = 1
    @Published private(set) var isGenerating = false
    @Published private(set) var triangleCount = 0
    @Published private(set) var vertexCount = 0
    @Published private(set) var geometry: SCNGeometry?

    init(volume: VolumeModel) {
        self.volume = volume
        // A new volume invalidates any existing surface.
        volume.$hasVolume
            .sink { [weak self] _ in
                self?.geometry = nil
                self?.triangleCount = 0
                self?.vertexCount = 0
            }
            .store(in: &cancellables)
    }

    func generate() {
        guard let h = volume.handle, !isGenerating else { return }
        lumen_mesh_snapshot(h)              // main thread: freeze the mask now
        isGenerating = true
        let bits = UInt(bitPattern: h)
        let smooth = Int32(max(0, smoothing))
        let ds = Int32(max(1, downsample))
        Task.detached(priority: .userInitiated) {
            guard let handle = OpaquePointer(bitPattern: bits) else { return }
            let tris = Int(lumen_mesh_generate(handle, smooth, ds)) // background
            // Hop back with the Sendable bit pattern only (an OpaquePointer is not
            // Sendable); the model re-reads its own handle on the main actor.
            await MainActor.run { self.finishGenerate(tris: tris) }
        }
    }

    private func finishGenerate(tris: Int) {
        guard let h = volume.handle else { isGenerating = false; return }
        triangleCount = tris
        vertexCount = Int(lumen_mesh_vertex_count(h))
        geometry = MeshBuilder.geometry(from: h)
        isGenerating = false
    }

    func exportSTL(to url: URL) -> Bool {
        guard let h = volume.handle else { return false }
        return lumen_mesh_write_stl(h, url.path) == 0
    }
}
