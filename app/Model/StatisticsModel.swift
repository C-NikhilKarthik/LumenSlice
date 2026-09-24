import SwiftUI
import Combine
import LumenCore

// One measured segment. Geometry is in millimetres (mm^3 / mm^2); the table renders
// friendlier units. HU fields describe the intensity distribution over the voxels.
struct SegmentStatsRow: Identifiable, Equatable {
    let id: Int
    var name: String
    var color: Color
    var voxelCount: Int
    var volumeMM3: Double       // label-map (voxel) volume - the reference figure
    var meshVolumeMM3: Double   // closed-surface enclosed volume (Slicer "Volume CS")
    var surfaceAreaMM2: Double  // closed-surface area, lightly smoothed
    var huMin: Double
    var huMax: Double
    var huMean: Double
    var huStdDev: Double
}

// Drives per-segment measurement through the C bridge and publishes a table of
// results. Same shape as MeshModel: measurement runs on a detached task reading a
// pinned handle (so a volume swap can't free the mask mid-compute), and results are
// published back on the main actor. The numbers are marked stale on any committed
// segmentation edit, so the table never silently shows figures for an older mask.
@MainActor
final class StatisticsModel: ObservableObject {
    private let volume: VolumeModel
    private let segmentation: SegmentationModel
    private var cancellables = Set<AnyCancellable>()

    @Published private(set) var rows: [SegmentStatsRow] = []
    @Published private(set) var isComputing = false
    // The mask changed since the last measurement, so the shown numbers are stale.
    @Published private(set) var isStale = false

    init(volume: VolumeModel, segmentation: SegmentationModel) {
        self.volume = volume
        self.segmentation = segmentation

        // A new (or cleared) volume drops the table entirely.
        volume.$hasVolume
            .sink { [weak self] _ in self?.clear() }
            .store(in: &cancellables)

        // Only committed edits (meshRevision) invalidate the numbers; threshold and
        // mask previews don't touch it, matching the 3D surface's invalidation.
        segmentation.$meshRevision
            .dropFirst()
            .sink { [weak self] _ in
                guard let self else { return }
                if !self.rows.isEmpty { self.isStale = true }
            }
            .store(in: &cancellables)
    }

    private func clear() {
        rows = []
        isStale = false
    }

    // Measure every non-empty segment. Snapshots the segment identity on the main
    // actor, measures each on a background task, then publishes. No-op while a
    // measurement is already running or when there is nothing to measure.
    func measure() {
        guard volume.handle != nil, !isComputing else { return }

        struct Spec { let id: Int32; let name: String; let color: Color }
        let specs: [Spec] = segmentation.segments
            .filter { $0.voxels > 0 }
            .map { Spec(id: Int32($0.id), name: $0.name, color: $0.color) }
        guard !specs.isEmpty else { clear(); return }

        // Pin the handle so loading a new volume mid-measure defers the free rather
        // than freeing the mask under the background reader. Released in finish().
        guard let pinned = volume.pinHandle() else { return }
        // Freeze the mask on the main actor before going off-thread, exactly like the
        // 3D mesh path. The background task then measures only this frozen snapshot,
        // so a concurrent main-actor edit (paint, threshold, undo) can't race it.
        lumen_seg_stats_snapshot(pinned)
        isComputing = true
        let bits = UInt(bitPattern: pinned)
        let statCount = Int(LUMEN_STAT_COUNT)

        Task.detached(priority: .userInitiated) {
            var built: [SegmentStatsRow] = []
            for spec in specs {
                guard let handle = OpaquePointer(bitPattern: bits) else { break }
                var buf = [Double](repeating: 0, count: statCount)
                buf.withUnsafeMutableBufferPointer { p in
                    lumen_seg_stats(handle, spec.id, p.baseAddress)
                }
                built.append(SegmentStatsRow(
                    id: Int(spec.id),
                    name: spec.name,
                    color: spec.color,
                    voxelCount: Int(buf[Int(LUMEN_STAT_VOXEL_COUNT)]),
                    volumeMM3: buf[Int(LUMEN_STAT_VOLUME_MM3)],
                    meshVolumeMM3: buf[Int(LUMEN_STAT_MESH_VOLUME_MM3)],
                    surfaceAreaMM2: buf[Int(LUMEN_STAT_SURFACE_AREA_MM2)],
                    huMin: buf[Int(LUMEN_STAT_HU_MIN)],
                    huMax: buf[Int(LUMEN_STAT_HU_MAX)],
                    huMean: buf[Int(LUMEN_STAT_HU_MEAN)],
                    huStdDev: buf[Int(LUMEN_STAT_HU_STDDEV)]))
            }
            let collected = built // immutable copy for the cross-actor hop
            await MainActor.run { self.finish(collected, from: bits) }
        }
    }

    private func finish(_ built: [SegmentStatsRow], from bits: UInt) {
        defer { isComputing = false; volume.releaseHandle() }
        // If the volume was swapped while measuring, the numbers belong to a
        // now-replaced handle — discard them rather than show them for a new scan.
        guard OpaquePointer(bitPattern: bits) == volume.handle else { return }
        rows = built
        isStale = false
    }

    // The current table as CSV (one header row + one row per segment). Empty string
    // when there is nothing measured.
    func csv() -> String {
        guard !rows.isEmpty else { return "" }
        var out = "Segment,Voxels,Volume LM (mm^3),Volume LM (cm^3),"
            + "Volume CS (mm^3),Volume CS (cm^3),Surface area (mm^2),"
            + "HU min,HU max,HU mean,HU stddev\n"
        for r in rows {
            let name = "\"" + r.name.replacingOccurrences(of: "\"", with: "\"\"") + "\""
            out += "\(name),\(r.voxelCount),"
                + "\(fmt(r.volumeMM3)),\(fmt(r.volumeMM3 / 1000.0)),"
                + "\(fmt(r.meshVolumeMM3)),\(fmt(r.meshVolumeMM3 / 1000.0)),"
                + "\(fmt(r.surfaceAreaMM2)),"
                + "\(fmt(r.huMin)),\(fmt(r.huMax)),\(fmt(r.huMean)),\(fmt(r.huStdDev))\n"
        }
        return out
    }

    // Fixed 4-decimal CSV formatting, independent of the user's locale (so the
    // separator is always a dot and never collides with the comma delimiter).
    private func fmt(_ v: Double) -> String {
        String(format: "%.4f", v)
    }
}
