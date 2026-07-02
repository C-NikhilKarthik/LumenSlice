import SwiftUI
import Combine
import LumenCore

// The active segmentation tool.
enum SegTool: String, CaseIterable, Identifiable {
    case threshold
    case regionGrow
    case paint
    case erase

    var id: String { rawValue }
    var title: String {
        switch self {
        case .threshold: return "Threshold"
        case .regionGrow: return "Grow"
        case .paint: return "Paint"
        case .erase: return "Erase"
        }
    }
    var icon: String {
        switch self {
        case .threshold: return "slider.horizontal.below.square.filled.and.square"
        case .regionGrow: return "drop.fill"
        case .paint: return "paintbrush.pointed.fill"
        case .erase: return "eraser.fill"
        }
    }
    // Tools that paint along a drag (vs. a single click / slider).
    var isBrush: Bool { self == .paint || self == .erase }
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

    @Published var tool: SegTool = .threshold {
        didSet { thresholdNeedsUndoCapture = true }
    }
    @Published var thresholdLo: Float = 150
    @Published var thresholdHi: Float = 3000
    @Published var tolerance: Float = 120
    @Published var brushRadius: Int = 12          // slice pixels
    @Published var removeSmallMin: Int = 50       // islands cutoff (voxels)
    @Published var showOverlay = true { didSet { refreshAllOverlays() } }

    @Published private(set) var segments: [SegmentRow] = []
    @Published var activeID: Int = 0
    @Published private(set) var voxelCount: Int = 0          // total, all segments
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
    // Coalesces a run of threshold applies (one tool session) into a single undo
    // entry: set true whenever the tool/segment changes, cleared on the first apply.
    private var thresholdNeedsUndoCapture = true
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

        // Threshold is apply-on-demand (the sidebar "Apply" button), not live: a
        // whole-volume threshold pass on every slider tick was the main source of
        // latency, so the user sets the cutoff then applies once.

        // Focus/slice changes -> re-extract the overlays for the new planes.
        volume.$focus
            .sink { [weak self] _ in
                guard let self, self.isActive else { return }
                self.refreshAllOverlays()
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
                self.overlayStore.images = [nil, nil, nil]
                if has {
                    self.reloadSegments()
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
        guard let h = volume.handle else { return }
        let (r, g, b) = Self.palette[nextColorIndex % Self.palette.count]
        nextColorIndex += 1
        let id = Int(lumen_seg_add(h, Int32(r * 255), Int32(g * 255), Int32(b * 255)))
        guard id > 0 else { return }
        reloadSegments()
        if tool == .threshold { tool = .paint } // new empty segment -> paint into it
        thresholdNeedsUndoCapture = true
    }

    func removeSegment(_ id: Int) {
        guard let h = volume.handle else { return }
        lumen_seg_push_undo(h)
        lumen_seg_remove(h, Int32(id))
        names[id] = nil
        reloadSegments()
        refreshAllOverlays()
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
        guard let h = volume.handle, activeID > 0 else { return }
        if thresholdNeedsUndoCapture {
            lumen_seg_push_undo(h)
            thresholdNeedsUndoCapture = false
        }
        lumen_seg_threshold(h, thresholdLo, thresholdHi)
        didMutateMask()
    }

    func seedRegionGrow(axis: Int, px: Int, py: Int) {
        guard let h = volume.handle, activeID > 0 else { return }
        var x: Int32 = 0, y: Int32 = 0, z: Int32 = 0
        lumen_slice_pixel_to_voxel(h, Int32(axis), Int32(volume.sliceIndex[axis]),
                                   Int32(px), Int32(py), &x, &y, &z)
        lumen_seg_push_undo(h)
        thresholdNeedsUndoCapture = true
        let added = lumen_seg_region_grow(h, x, y, z, tolerance)
        if added > 0 { didMutateMask() } else { refreshUndoState() }
    }

    // Paint strokes: capture one undo entry at the start, paint per drag tick (only
    // re-extracting the painted plane for responsiveness), settle on stroke end.
    func beginStroke() {
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

    func endStroke() { didMutateMask() }

    // MARK: - Grow from seeds (competitive, multi-label)

    // Open while a grow session runs (after Initialise, before Apply/Cancel). Only
    // a UI concern - each grow is an ordinary undoable edit, so Cmd-Z works too.
    @Published var growSessionActive = false

    // Segments compete for the background, so this needs at least two of them
    // (typically the structure plus a background segment).
    var canGrowFromSeeds: Bool { segments.count >= 2 }

    // Initialise: snapshot, then grow every segment's painted seeds outward by one
    // tolerance band. Cancel reverts this via undo; further Updates grow more.
    func growInitialise() {
        guard let h = volume.handle, canGrowFromSeeds else { return }
        lumen_seg_push_undo(h)
        thresholdNeedsUndoCapture = true
        growSessionActive = true
        _ = lumen_seg_grow_from_seeds(h, tolerance)
        didMutateMask()
    }

    // Update: expand another tolerance band from the enlarged regions (or from any
    // seeds the user painted since). Its own undo step.
    func growUpdate() {
        guard let h = volume.handle, growSessionActive else { return }
        lumen_seg_push_undo(h)
        _ = lumen_seg_grow_from_seeds(h, tolerance)
        didMutateMask()
    }

    // Cancel reverts the last grow; Apply just closes the session (the result stays,
    // reversible from the undo stack / Cmd-Z).
    func growCancel() {
        if growSessionActive { undo() }
        growSessionActive = false
    }

    func growApply() { growSessionActive = false }

    func keepLargest() {
        guard let h = volume.handle, activeID > 0 else { return }
        lumen_seg_push_undo(h)
        thresholdNeedsUndoCapture = true
        if lumen_seg_keep_largest(h) > 0 { didMutateMask() } else { refreshUndoState() }
    }

    func removeSmall() {
        guard let h = volume.handle, activeID > 0 else { return }
        lumen_seg_push_undo(h)
        thresholdNeedsUndoCapture = true
        if lumen_seg_remove_small(h, Int(removeSmallMin)) > 0 { didMutateMask() }
        else { refreshUndoState() }
    }

    func growMargin() { applyMorphology { lumen_seg_grow($0, 1) } }
    func shrinkMargin() { applyMorphology { lumen_seg_shrink($0, 1) } }
    func smooth() { applyMorphology { lumen_seg_smooth($0, 1) } }

    private func applyMorphology(_ op: (OpaquePointer) -> Int) {
        guard let h = volume.handle, activeID > 0 else { return }
        lumen_seg_push_undo(h)
        thresholdNeedsUndoCapture = true
        if op(h) > 0 { didMutateMask() } else { refreshUndoState() }
    }

    func clearActive() {
        guard let h = volume.handle, activeID > 0 else { return }
        lumen_seg_push_undo(h)
        thresholdNeedsUndoCapture = true
        lumen_seg_clear(h)
        didMutateMask()
    }

    func undo() {
        guard let h = volume.handle else { return }
        if lumen_seg_undo(h) != 0 {
            thresholdNeedsUndoCapture = true
            didMutateMask()
        }
    }

    func redo() {
        guard let h = volume.handle else { return }
        if lumen_seg_redo(h) != 0 {
            thresholdNeedsUndoCapture = true
            didMutateMask()
        }
    }

    // MARK: - Overlay extraction

    private func didMutateMask() {
        reloadSegments()       // refresh per-segment + total voxel counts + undo state
        refreshAllOverlays()
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

    private func refreshOverlay(_ axis: Int) {
        guard let h = volume.handle, showOverlay else {
            overlayStore.images[axis] = nil
            return
        }
        var w: Int32 = 0, ht: Int32 = 0
        guard let ptr = lumen_extract_mask_slice(h, Int32(axis),
                                                 Int32(volume.sliceIndex[axis]),
                                                 &w, &ht),
              w > 0, ht > 0 else {
            overlayStore.images[axis] = nil
            return
        }
        let data = Data(bytes: ptr, count: Int(w) * Int(ht) * 4) // copy out of scratch
        overlayStore.images[axis] = VolumeModel.makeImage(
            data: data, width: Int(w), height: Int(ht))
    }
}
