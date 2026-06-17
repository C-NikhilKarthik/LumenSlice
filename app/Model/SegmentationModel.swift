import SwiftUI
import Combine
import LumenCore

// The active segmentation tool (paint/erase land in P1b).
enum SegTool: String, CaseIterable, Identifiable {
    case threshold
    case regionGrow

    var id: String { rawValue }
    var title: String {
        switch self {
        case .threshold: return "Threshold"
        case .regionGrow: return "Region grow"
        }
    }
    var icon: String {
        switch self {
        case .threshold: return "slider.horizontal.below.square.filled.and.square"
        case .regionGrow: return "drop.fill"
        }
    }
}

// Drives the C++ segmentation mask through the bridge and republishes a colored
// overlay CGImage per plane. It shares VolumeModel's volume handle (read-only) and
// re-extracts overlays whenever the slice indices change, a new volume loads, or
// the mask is mutated — but only while the Segment tab is active, to avoid doing
// overlay work the user can't see.
@MainActor
final class SegmentationModel: ObservableObject {
    private let volume: VolumeModel
    private var cancellables = Set<AnyCancellable>()

    @Published var tool: SegTool = .threshold
    @Published var thresholdLo: Float = 150
    @Published var thresholdHi: Float = 3000
    @Published var tolerance: Float = 120
    @Published var showOverlay = true
    @Published private(set) var voxelCount: Int = 0
    @Published private(set) var overlays: [CGImage?] = [nil, nil, nil]

    // Set by the shell when the Segment tab is shown/hidden.
    var isActive = false {
        didSet { if isActive && !oldValue { refreshAllOverlays() } }
    }

    init(volume: VolumeModel) {
        self.volume = volume

        // Live threshold: debounce the HU range so dragging doesn't recompute the
        // whole-volume mask on every tick. Threshold replaces the mask, so it only
        // makes sense while that tool is selected.
        Publishers.CombineLatest($thresholdLo, $thresholdHi)
            .debounce(for: .milliseconds(180), scheduler: RunLoop.main)
            .sink { [weak self] _, _ in
                guard let self, self.tool == .threshold else { return }
                self.applyThreshold()
            }
            .store(in: &cancellables)

        // Slice scrubbing -> re-extract the affected plane's overlay.
        volume.$sliceIndex
            .sink { [weak self] _ in
                guard let self, self.isActive else { return }
                self.refreshAllOverlays()
            }
            .store(in: &cancellables)

        // A fresh volume resets the mask (the bridge clears it on load).
        volume.$hasVolume
            .sink { [weak self] has in
                guard let self else { return }
                self.voxelCount = 0
                self.overlays = [nil, nil, nil]
                if has, self.isActive { self.refreshAllOverlays() }
            }
            .store(in: &cancellables)
    }

    // MARK: - Operations

    func applyThreshold() {
        guard let h = volume.handle else { return }
        lumen_seg_threshold(h, thresholdLo, thresholdHi)
        didMutateMask()
    }

    func seedRegionGrow(axis: Int, px: Int, py: Int) {
        guard let h = volume.handle else { return }
        var x: Int32 = 0, y: Int32 = 0, z: Int32 = 0
        lumen_slice_pixel_to_voxel(h, Int32(axis), Int32(volume.sliceIndex[axis]),
                                   Int32(px), Int32(py), &x, &y, &z)
        let added = lumen_seg_region_grow(h, x, y, z, tolerance)
        if added > 0 { didMutateMask() }
    }

    func clear() {
        guard let h = volume.handle else { return }
        lumen_seg_clear(h)
        didMutateMask()
    }

    // MARK: - Overlay extraction

    private func didMutateMask() {
        guard let h = volume.handle else { return }
        voxelCount = Int(lumen_seg_count(h))
        refreshAllOverlays()
    }

    private func refreshAllOverlays() {
        guard volume.hasVolume else { return }
        for axis in 0..<3 { refreshOverlay(axis) }
    }

    private func refreshOverlay(_ axis: Int) {
        guard let h = volume.handle else { overlays[axis] = nil; return }
        var w: Int32 = 0, ht: Int32 = 0
        guard let ptr = lumen_extract_mask_slice(h, Int32(axis),
                                                 Int32(volume.sliceIndex[axis]),
                                                 &w, &ht),
              w > 0, ht > 0 else {
            overlays[axis] = nil
            return
        }
        let data = Data(bytes: ptr, count: Int(w) * Int(ht) * 4) // copy out of scratch
        overlays[axis] = VolumeModel.makeImage(data: data, width: Int(w), height: Int(ht))
    }
}
