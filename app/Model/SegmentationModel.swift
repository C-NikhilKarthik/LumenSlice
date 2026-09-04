import SwiftUI
import Combine
import LumenCore

// The active segmentation tool.
enum SegTool: String, CaseIterable, Identifiable {
    case threshold
    case regionGrow
    case levelTrace
    case paint
    case erase

    var id: String { rawValue }
    var title: String {
        switch self {
        case .threshold: return "Threshold"
        case .regionGrow: return "Fill"
        case .levelTrace: return "Level Trace"
        case .paint: return "Paint"
        case .erase: return "Erase"
        }
    }
    var icon: String {
        switch self {
        case .threshold: return "slider.horizontal.below.square.filled.and.square"
        case .regionGrow: return "drop.fill"
        case .levelTrace: return "camera.filters"
        case .paint: return "paintbrush.pointed.fill"
        case .erase: return "eraser.fill"
        }
    }
    // Tools that paint along a drag (vs. a single click / slider).
    var isBrush: Bool { self == .paint || self == .erase }
    // Tools driven by a single click on a slice (vs. a drag or a slider).
    var isClickSeed: Bool { self == .regionGrow || self == .levelTrace }
}

// One row in the segment list. `id` is the C++ label byte (1..255); `name` lives
// only here (UI-side), colour + visibility mirror the C++ SegmentTable.
struct SegmentRow: Identifiable, Equatable {
    let id: Int
    var name: String
    var color: Color
    var visible: Bool
    var voxels: Int
}

// The per-plane mask overlay images, in their own tiny observable so that a paint
// stroke (which republishes only these) re-renders the slice panes WITHOUT churning
// the segment-control panel that observes the heavier SegmentationModel. This is the
// difference between sluggish and fluid brushing.
@MainActor
final class OverlayStore: ObservableObject {
    @Published var images: [CGImage?] = [nil, nil, nil]
}

// Drives the C++ multi-segment mask through the bridge and republishes a colored
// overlay CGImage per plane. Shares VolumeModel's volume handle (read-only for the
// volume; it owns the mask + segment table living in the same C++ handle). All
// edits target the active segment and are bracketed by undo snapshots.
@MainActor
final class SegmentationModel: ObservableObject {
    private let volume: VolumeModel
    private var cancellables = Set<AnyCancellable>()

    // Open the Segment tab ready to paint (the common first action). Threshold is
    // one selection away; selecting it arms its preview (see the didSet).
    @Published var tool: SegTool = .paint {
        didSet {
            thresholdNeedsUndoCapture = true
            // Selecting the threshold tool is an explicit engagement, so the
            // preview may show from here on.
            if tool == .threshold { thresholdPreviewArmed = true }
            if oldValue != tool { refreshAllOverlays() }
        }
    }
    @Published var thresholdLo: Float = 150
    @Published var thresholdHi: Float = 3000
    @Published var tolerance: Float = 120
    @Published var brushRadius: Int = 12          // slice pixels
    @Published var growSeedLocality: Float = 0   // Slicer-style distance penalty
    @Published private(set) var growPreviewActive = false
    @Published var showOverlay = true { didSet { refreshAllOverlays() } }
    @Published var isBusy = false
    // An editable-area (intensity) mask is active: paint additions are clipped to
    // maskRange. The UI shows an indicator and a Deactivate button while true.
    @Published private(set) var maskActive = false
    @Published private(set) var maskRange: ClosedRange<Float> = 0...0

    @Published private(set) var segments: [SegmentRow] = []
    @Published var activeID: Int = 0
    @Published private(set) var voxelCount: Int = 0          // total, all segments
    // Incremented only for operations that are allowed to invalidate the 3D
    // surface. Threshold slider/mask previews intentionally do not touch it.
    @Published private(set) var meshRevision = 0
    @Published private(set) var canUndo = false
    @Published private(set) var canRedo = false
    // Overlay images live in their own store (see OverlayStore) so brushing stays fluid.
    let overlayStore = OverlayStore()

    // Names survive list rebuilds (the bridge only knows ids/colours/visibility).
    private var names: [Int: String] = [:]
    private var nextSegmentNumber = 1
    // Monotonic palette cursor: advancing it (never resetting on delete) means a
    // fresh segment never reuses a colour still on screen just because a middle
    // segment was removed and the list count shrank.
    private var nextColorIndex = 0
    // The threshold effect shows a live preview overlay. It must stay hidden until
    // the user actually engages threshold (selects the tool, drags the range, or
    // runs Otsu); otherwise a freshly loaded scan looks like a segment is already
    // marked. Reset to false on every load; armed at the real engagement points.
    private var thresholdPreviewArmed = false
    // Coalesces a run of live-threshold edits into a single undo entry.
    private var thresholdNeedsUndoCapture = true
    // Set when we apply a threshold programmatically (Otsu) so the debounced
    // CombineLatest sink skips the redundant second full-volume threshold pass.
    private var skipNextThresholdSink = false
    // Wall-clock of the last overlay rebuild during the active brush stroke. Each
    // rebuild re-extracts the whole painted slice (cost grows with slice size — and
    // coronal/sagittal slices grow with the file count), so we cap it to display
    // rate while painting; endStroke does the final exact refresh.
    private var lastStrokeOverlayTime: CFTimeInterval = 0
    private static let strokeOverlayInterval: CFTimeInterval = 1.0 / 60.0

    // Distinct, readable segment colours, cycled as segments are added.
    static let palette: [(Double, Double, Double)] = [
        (0.00, 0.71, 0.82), (0.91, 0.35, 0.31), (0.47, 0.78, 0.35),
        (0.96, 0.75, 0.27), (0.63, 0.47, 0.86), (0.94, 0.55, 0.78),
        (0.35, 0.78, 0.78), (0.82, 0.59, 0.35),
    ]

    // The palette as SwiftUI Colors, for the segment colour-swatch picker.
    static var paletteColors: [Color] {
        palette.map { Color(.sRGB, red: $0.0, green: $0.1, blue: $0.2) }
    }

    // Set by the shell when the Segment tab is shown/hidden.
    var isActive = false {
        didSet { if isActive && !oldValue { refreshAllOverlays() } }
    }

    init(volume: VolumeModel) {
        self.volume = volume

        // Live threshold: debounce so dragging doesn't recompute the whole-volume
        // mask on every tick. Only meaningful while the threshold tool is selected.
        Publishers.CombineLatest($thresholdLo, $thresholdHi)
            .debounce(for: .milliseconds(180), scheduler: RunLoop.main)
            .sink { [weak self] _, _ in
                guard let self else { return }
                if self.skipNextThresholdSink { self.skipNextThresholdSink = false; return }
                guard self.tool == .threshold else { return }
                // A real user drag of the range engages the preview.
                self.thresholdPreviewArmed = true
                self.applyThreshold()
            }
            .store(in: &cancellables)

        // Focus/slice changes -> re-extract the overlays for the new planes. Coalesced
        // the same way the grayscale slices are (VolumeModel.scheduleRefresh): a fast
        // scroll fires many focus changes, and re-extracting the mask overlays on each
        // one keeps the Segment tab from scrolling as smoothly as Visualize. Drop the
        // intermediate overlays and refresh once on the next runloop tick.
        volume.$focus
            .sink { [weak self] _ in
                guard let self, self.isActive else { return }
                self.scheduleOverlayRefresh()
            }
            .store(in: &cancellables)

        // A fresh volume resets the mask + segment table (the bridge re-creates one
        // default segment on load); rebuild our mirror to match.
        volume.$hasVolume
            .sink { [weak self] has in
                guard let self else { return }
                self.names.removeAll()
                self.nextSegmentNumber = 1
                self.nextColorIndex = 0
                self.growPreviewActive = false
                // A fresh scan starts with no threshold preview, so an untouched
                // load never looks pre-segmented.
                self.thresholdPreviewArmed = false
                // The bridge editor resets its intensity mask on load; mirror that.
                self.maskActive = false
                self.overlayStore.images = [nil, nil, nil]
                if has {
                    self.reloadSegments()
                    // Reconcile the threshold window with this scan's HU range so the
                    // slider and the stored values agree (a previous scan may have left
                    // an out-of-range window). Guard the live sink so this housekeeping
                    // never auto-thresholds on load.
                    let loBound = self.volume.huLo
                    let hiBound = max(self.volume.huHi, loBound + 1)
                    let newLo = min(max(self.thresholdLo, loBound), hiBound)
                    let newHi = min(max(self.thresholdHi, newLo), hiBound)
                    if newLo != self.thresholdLo || newHi != self.thresholdHi {
                        self.skipNextThresholdSink = true
                        self.thresholdLo = newLo
                        self.thresholdHi = newHi
                    }
                    if self.isActive { self.refreshAllOverlays() }
                } else {
                    self.segments = []
                    self.activeID = 0
                    self.voxelCount = 0
                }
            }
            .store(in: &cancellables)
    }

    var activeColor: Color {
        segments.first { $0.id == activeID }?.color ?? .accentColor
    }

    // Segments that currently hold seed voxels. Grow-from-seeds partitions the
    // region *between* seeds, so (as in 3D Slicer) it needs at least two seeded
    // segments — typically the structure and a background — before it can run.
    var seededSegmentCount: Int {
        segments.reduce(0) { $0 + ($1.voxels > 0 ? 1 : 0) }
    }
    var canGrowFromSeeds: Bool { seededSegmentCount >= 2 }

    // MARK: - Segment list

    func reloadSegments() {
        guard let h = volume.handle else { segments = []; return }
        // One pass over the mask yields every label's voxel count, instead of a
        // full-volume scan per segment plus another for the total. On a large scan
        // with several segments this is the difference between a smooth edit and a
        // multi-hundred-ms hitch at the end of every brush stroke.
        var histogram = [Int](repeating: 0, count: 256)
        histogram.withUnsafeMutableBufferPointer { buf in
            lumen_seg_label_histogram(h, buf.baseAddress)
        }
        let count = Int(lumen_seg_segment_count(h))
        var rows: [SegmentRow] = []
        rows.reserveCapacity(count)
        for i in 0..<count {
            let id = Int(lumen_seg_segment_id_at(h, Int32(i)))
            guard id > 0 else { continue }
            var r: Int32 = 0, g: Int32 = 0, b: Int32 = 0
            lumen_seg_get_color(h, Int32(id), &r, &g, &b)
            let visible = lumen_seg_get_visible(h, Int32(id)) != 0
            let voxels = id < histogram.count ? histogram[id] : 0
            let name = names[id] ?? defaultName(for: id)
            names[id] = name
            rows.append(SegmentRow(
                id: id,
                name: name,
                color: Color(.sRGB, red: Double(r) / 255, green: Double(g) / 255,
                             blue: Double(b) / 255),
                visible: visible,
                voxels: voxels))
        }
        segments = rows
        activeID = Int(lumen_seg_active(h))
        voxelCount = histogram[1...].reduce(0, +)   // total labelled = all non-bg
        refreshUndoState()
    }

    private func defaultName(for id: Int) -> String {
        let name = "Segment \(nextSegmentNumber)"
        nextSegmentNumber += 1
        return name
    }

    func addSegment() {
        guard let h = volume.handle, !isBusy, !growPreviewActive else { return }
        let (r, g, b) = Self.palette[nextColorIndex % Self.palette.count]
        nextColorIndex += 1
        let id = Int(lumen_seg_add(h, Int32(r * 255), Int32(g * 255), Int32(b * 255)))
        guard id > 0 else { return }
        reloadSegments()
        if tool == .threshold { tool = .paint } // new empty segment -> paint into it
        thresholdNeedsUndoCapture = true
    }

    func removeSegment(_ id: Int) {
        guard let h = volume.handle, !isBusy else { return }
        if growPreviewActive { cancelGrowPreview() }
        lumen_seg_push_undo(h)
        lumen_seg_remove(h, Int32(id))
        names[id] = nil
        // Deleting a segment must not leave its threshold preview painted over
        // the slices. Clear the published images first so no stale frame survives
        // while the replacement mask overlay is being extracted.
        thresholdPreviewArmed = false
        overlayStore.images = [nil, nil, nil]
        if tool == .threshold { tool = .paint }
        reloadSegments()
        refreshAllOverlays()
        meshRevision &+= 1
    }

    func setActive(_ id: Int) {
        guard let h = volume.handle else { return }
        lumen_seg_set_active(h, Int32(id))
        activeID = id
        thresholdNeedsUndoCapture = true
    }

    func setVisible(_ id: Int, _ visible: Bool) {
        guard let h = volume.handle else { return }
        lumen_seg_set_visible(h, Int32(id), visible ? 1 : 0)
        reloadSegments()
        refreshAllOverlays()
    }

    func setColor(_ id: Int, _ color: Color) {
        guard let h = volume.handle else { return }
        let (r, g, b) = rgb(color)
        lumen_seg_set_color(h, Int32(id), Int32(r), Int32(g), Int32(b))
        reloadSegments()
        refreshAllOverlays()
    }

    func rename(_ id: Int, to name: String) {
        names[id] = name.isEmpty ? defaultName(for: id) : name
        reloadSegments()
    }

    private func rgb(_ color: Color) -> (Int, Int, Int) {
        let ns = NSColor(color).usingColorSpace(.sRGB) ?? .gray
        return (Int(ns.redComponent * 255), Int(ns.greenComponent * 255),
                Int(ns.blueComponent * 255))
    }

    // MARK: - Editing operations

    func applyThreshold() {
        cancelGrowPreviewForEditing()
        // Slicer's Threshold effect is a non-destructive preview. Keep the
        // actual labelmap untouched until the user explicitly presses Apply.
        guard volume.handle != nil, activeID > 0 else { return }
        refreshAllOverlays()
    }

    // Threshold changes are previewed in the three slice panes. This explicit
    // action is the commit point that permits a 3D surface rebuild.
    func commitThreshold() {
        guard let h = volume.handle, activeID > 0, !isBusy else { return }
        let lo = thresholdLo
        let hi = thresholdHi
        runAsyncMaskOperation(h, committed: true) { _ in
            lumen_seg_threshold(h, lo, hi)
            return 0
        }
    }

    func applyOtsu() {
        cancelGrowPreviewForEditing()
        guard let h = volume.handle, activeID > 0 else { return }
        let t = lumen_seg_otsu(h)
        // Otsu only chooses the preview window. The explicit Apply button is the
        // commit point, just as it is for a manually selected Threshold range.
        skipNextThresholdSink = true
        thresholdLo = t
        thresholdHi = volume.huHi
        // Running Otsu is an explicit engagement, so the preview may show.
        thresholdPreviewArmed = true
        refreshAllOverlays()
    }

    func seedRegionGrow(axis: Int, px: Int, py: Int) {
        cancelGrowPreviewForEditing()
        guard let h = volume.handle, activeID > 0 else { return }
        var x: Int32 = 0, y: Int32 = 0, z: Int32 = 0
        lumen_slice_pixel_to_voxel(h, Int32(axis), Int32(volume.sliceIndex[axis]),
                                   Int32(px), Int32(py), &x, &y, &z)
        let tolerance = self.tolerance
        runAsyncMaskOperation(h, committed: false) { _ in
            Int64(lumen_seg_region_grow(h, x, y, z, tolerance))
        }
    }

    // Level trace: one click selects the iso-level (HU >= clicked pixel) region on
    // the clicked slice. The C++ kernel maps the pixel to a voxel itself, so we pass
    // slice-pixel coordinates straight through (unlike Fill, which seeds a voxel).
    func seedLevelTrace(axis: Int, px: Int, py: Int) {
        cancelGrowPreviewForEditing()
        guard let h = volume.handle, activeID > 0 else { return }
        let index = Int32(volume.sliceIndex[axis])
        runAsyncMaskOperation(h, committed: false) { _ in
            Int64(lumen_seg_level_trace(h, Int32(axis), index, Int32(px), Int32(py)))
        }
    }

    func applyMask() {
        guard let h = volume.handle, activeID > 0, !isBusy else { return }
        let lo = min(thresholdLo, thresholdHi)
        let hi = max(thresholdLo, thresholdHi)
        runAsyncMaskOperation(h, committed: false, captureUndo: false) { _ in
            lumen_seg_apply_mask(h, lo, hi)
            return 0
        }
        maskActive = true
        maskRange = lo...hi
        tool = .paint
    }

    // Turn off the editable-area mask so painting is unconstrained again. Guarded
    // on isBusy like every other mask mutation, so it never races a detached op
    // that holds the pinned handle.
    func deactivateMask() {
        guard let h = volume.handle, !isBusy else { return }
        lumen_seg_clear_mask(h)
        maskActive = false
    }

    // Retune an active mask: drop it and jump back to the Threshold tool with the
    // same range still loaded, so the user tweaks the range and re-applies in one
    // step instead of starting over.
    func editMaskRange() {
        guard !isBusy else { return }
        deactivateMask()
        tool = .threshold   // didSet arms the preview so the range is visible again
    }

    private func runAsyncMaskOperation(_ handle: OpaquePointer, committed: Bool = true,
                                        captureUndo: Bool = true,
                                        _ operation: @escaping (OpaquePointer) -> Int64) {
        guard !isBusy else { return }
        guard let pinnedHandle = volume.pinHandle() else { return }
        if captureUndo { lumen_seg_push_undo(handle) }
        thresholdNeedsUndoCapture = true
        isBusy = true
        Task.detached(priority: .userInitiated) {
            _ = operation(pinnedHandle)
            await MainActor.run {
                self.volume.releaseHandle()
                self.isBusy = false
                self.didMutateMask(committed: committed)
            }
        }
    }

    // Paint strokes: capture one undo entry at the start, paint per drag tick (only
    // re-extracting the painted plane for responsiveness), settle on stroke end.
    func beginStroke() {
        cancelGrowPreviewForEditing()
        guard !isBusy else { return }
        guard let h = volume.handle else { return }
        lumen_seg_push_undo(h)
        thresholdNeedsUndoCapture = true
        lastStrokeOverlayTime = 0   // first move in the stroke refreshes immediately
        refreshUndoState()
    }

    // Paint from the previous brush point to the current one, stamping overlapping
    // disks along the segment so fast drags leave no gaps (3D-Slicer-style). Only
    // the painted plane's overlay is re-extracted, and only if something changed.
    func paintStroke(axis: Int, from: (px: Int, py: Int)?, to: (px: Int, py: Int)) {
        guard !isBusy else { return }
        guard let h = volume.handle, activeID > 0, tool.isBrush else { return }
        let add: Int32 = tool == .paint ? 1 : 0
        let idx = Int32(volume.sliceIndex[axis])
        let r = Int32(brushRadius)
        var changed: Int64 = 0

        func stamp(_ x: Int, _ y: Int) {
            changed += Int64(lumen_seg_paint(h, Int32(axis), idx, Int32(x), Int32(y), r, add))
        }

        if let from {
            let dx = to.px - from.px, dy = to.py - from.py
            let span = max(abs(dx), abs(dy))
            // Step <= half the radius so consecutive stamps overlap into a line.
            let stepCount = max(1, span / max(1, brushRadius / 2))
            for s in 0...stepCount {
                let t = Double(s) / Double(stepCount)
                stamp(Int((Double(from.px) + t * Double(dx)).rounded()),
                      Int((Double(from.py) + t * Double(dy)).rounded()))
            }
        } else {
            stamp(to.px, to.py)
        }
        // Throttle the live overlay rebuild to display rate; intermediate frames are
        // dropped (the paint itself already landed in the mask), and endStroke draws
        // the final exact overlay for every plane.
        if changed > 0 {
            let now = CFAbsoluteTimeGetCurrent()
            if now - lastStrokeOverlayTime >= Self.strokeOverlayInterval {
                lastStrokeOverlayTime = now
                refreshOverlay(axis)
            }
        }
    }

    func endStroke() { didMutateMask(committed: true) }

    // Slicer-style Grow from Seeds preview. The operation is kept as one undoable
    // mask change so Cancel restores the painted seeds and Apply leaves the result
    // available to Undo later.
    func initializeGrowPreview() {
        guard let h = volume.handle, canGrowFromSeeds else { return }
        if growPreviewActive { return }
        lumen_seg_push_undo(h)
        thresholdNeedsUndoCapture = true
        if lumen_seg_grow_from_seeds(h, 1, growSeedLocality) > 0 {
            growPreviewActive = true
            didMutateMask(committed: true)
        }
        else { refreshUndoState() }
    }

    func applyGrowPreview() { growPreviewActive = false; refreshUndoState() }

    func cancelGrowPreview() {
        guard growPreviewActive, let h = volume.handle else { return }
        growPreviewActive = false
        if lumen_seg_undo(h) != 0 { didMutateMask(committed: true) }
        else { refreshUndoState() }
    }

    private func cancelGrowPreviewForEditing() {
        if growPreviewActive { cancelGrowPreview() }
    }

    // Erase labelled voxels by a screen-space lasso drawn over the 3D surface. The
    // caller (the 3D pane) supplies the combined view*projection matrix (16 floats,
    // row-major) SceneKit is using, the viewport size, and the outline as a flat
    // [x0,y0,x1,y1,…] pixel list in the same top-left/y-down space. `onlyLabel` == 0
    // cuts every labelled voxel inside the outline. Returns whether anything changed.
    @discardableResult
    func scissorCut(mvp: [Float], viewportWidth: Int, viewportHeight: Int,
                    polygon: [Float], eraseInside: Bool = true,
                    onlyLabel: Int = 0) -> Bool {
        cancelGrowPreviewForEditing()
        guard let h = volume.handle, mvp.count == 16, polygon.count >= 6 else {
            return false
        }
        lumen_seg_push_undo(h)
        thresholdNeedsUndoCapture = true
        let cleared = mvp.withUnsafeBufferPointer { m in
            polygon.withUnsafeBufferPointer { p in
                lumen_seg_scissor_cut(h, m.baseAddress, Int32(viewportWidth),
                                      Int32(viewportHeight), p.baseAddress,
                                      Int32(polygon.count / 2), eraseInside ? 1 : 0,
                                      Int32(onlyLabel))
            }
        }
        if cleared > 0 { didMutateMask(committed: true); return true }
        refreshUndoState()
        return false
    }

    func clearActive() {
        cancelGrowPreviewForEditing()
        guard let h = volume.handle, activeID > 0 else { return }
        lumen_seg_push_undo(h)
        thresholdNeedsUndoCapture = true
        lumen_seg_clear(h)
        didMutateMask(committed: true)
    }

    func undo() {
        guard let h = volume.handle else { return }
        growPreviewActive = false
        if lumen_seg_undo(h) != 0 {
            thresholdNeedsUndoCapture = true
            didMutateMask(committed: true)
        }
    }

    func redo() {
        guard let h = volume.handle else { return }
        growPreviewActive = false
        if lumen_seg_redo(h) != 0 {
            thresholdNeedsUndoCapture = true
            didMutateMask(committed: true)
        }
    }

    // MARK: - Overlay extraction

    private func didMutateMask(committed: Bool = true) {
        reloadSegments()       // refresh per-segment + total voxel counts + undo state
        refreshAllOverlays()
        if committed { meshRevision &+= 1 }
    }

    private func refreshUndoState() {
        guard let h = volume.handle else { canUndo = false; canRedo = false; return }
        canUndo = lumen_seg_can_undo(h) != 0
        canRedo = lumen_seg_can_redo(h) != 0
    }

    private func refreshAllOverlays() {
        guard volume.hasVolume else { return }
        for axis in 0..<3 { refreshOverlay(axis) }
    }

    // Coalesced overlay refresh for the scroll/nav path (see the $focus sink): a burst
    // of focus changes collapses to a single refresh on the next runloop tick.
    private var overlayRefreshScheduled = false
    private func scheduleOverlayRefresh() {
        guard !overlayRefreshScheduled else { return }
        overlayRefreshScheduled = true
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.overlayRefreshScheduled = false
            self.refreshAllOverlays()
        }
    }

    private func refreshOverlay(_ axis: Int) {
        guard let h = volume.handle, showOverlay else {
            overlayStore.images[axis] = nil
            return
        }
        var w: Int32 = 0, ht: Int32 = 0
        let ptr: UnsafePointer<UInt8>?
        // Show the live threshold preview only once the user has engaged threshold;
        // an untouched load falls through to the (empty) mask overlay instead.
        if tool == .threshold && thresholdPreviewArmed {
            ptr = lumen_extract_threshold_slice(h, Int32(axis),
                                                Int32(volume.sliceIndex[axis]),
                                                thresholdLo, thresholdHi, &w, &ht)
        } else {
            ptr = lumen_extract_mask_slice(h, Int32(axis),
                                           Int32(volume.sliceIndex[axis]), &w, &ht)
        }
        guard let ptr,
              w > 0, ht > 0 else {
            overlayStore.images[axis] = nil
            return
        }
        let data = Data(bytes: ptr, count: Int(w) * Int(ht) * 4) // copy out of scratch
        overlayStore.images[axis] = VolumeModel.makeImage(
            data: data, width: Int(w), height: Int(ht))
    }
}
