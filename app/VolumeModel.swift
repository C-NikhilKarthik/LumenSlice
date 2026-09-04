import SwiftUI
import LumenCore

// One selectable DICOM series from a folder scan, shown in the multi-series picker.
// `id` is the series index into the live scan (largest-first), which is what gets
// passed back to VolumeModel.chooseSeries.
struct SeriesOption: Identifiable, Hashable {
    let id: Int
    let description: String
    let modality: String
    let sliceCount: Int
    let width: Int
    let height: Int
    let created: String

    // Primary label: the series description, or "Series N" when it has none.
    var title: String {
        description.isEmpty ? "Series \(id + 1)" : description
    }

    // "512 × 512", or empty when the scan didn't report a pixel size.
    var sizeText: String {
        (width > 0 && height > 0) ? "\(width) × \(height)" : ""
    }
}

// Observable bridge between SwiftUI and the C++ core. Holds the loaded volume
// handle and republishes CGImages for the three slice planes whenever the user
// scrolls or adjusts window/level.
@MainActor
final class VolumeModel: ObservableObject {
    // Readable by the SegmentationModel / MeshModel (same module), which drive the
    // same C++ volume handle; only VolumeModel mutates it (load/free).
    private(set) var handle: OpaquePointer?

    // Background mesh generation reads the handle off the main actor. A load that
    // lands while that work is in flight must NOT free the handle underneath it, so
    // a pinned handle is retired into `deferredFree` and released only once the last
    // background reader balances its pin. All of this is touched on the main actor
    // (finishLoad, pinHandle, releaseHandle), so the counter needs no locking.
    private var pinnedReaders = 0
    private var deferredFree: [OpaquePointer] = []

    // Live header-only folder scan kept alive between opening a multi-series folder
    // and the user's pick in the series sheet. Freed on choose, cancel, a superseding
    // scan, or deinit - never leaked. Touched only on the main actor.
    private var scanHandle: OpaquePointer?

    /// Pin the current handle so an in-flight load won't free it while background
    /// work (e.g. marching cubes) is reading it. Returns nil if no volume is loaded.
    /// Every successful pin MUST be balanced by exactly one `releaseHandle()`.
    func pinHandle() -> OpaquePointer? {
        guard let h = handle else { return nil }
        pinnedReaders += 1
        return h
    }

    /// Balance a `pinHandle()`. When the last pin is released, any handles a load
    /// retired while pinned are freed.
    func releaseHandle() {
        pinnedReaders = max(0, pinnedReaders - 1)
        if pinnedReaders == 0 {
            for h in deferredFree { lumen_free(h) }
            deferredFree.removeAll()
        }
    }

    @Published var status = "Open a DICOM folder to begin."
    @Published var hasVolume = false
    @Published var isLoading = false

    // Non-nil when a scanned folder holds several series: the picker sheet reads this
    // to list them. Cleared once the user chooses a series or cancels.
    @Published var pendingSeries: [SeriesOption]?

    // Geometry.
    @Published var width = 0
    @Published var height = 0
    @Published var depth = 0
    @Published var spacing = SIMD3<Float>(1, 1, 1)
    @Published var huLo: Float = -1000
    @Published var huHi: Float = 1000

    // Window/level transfer function (HU). Each setter re-renders all three
    // planes; use setWindowLevel(level:window:) to change both with one refresh
    // (e.g. presets, or the drag-on-image gesture).
    @Published var level: Float = 40 { didSet { if !suppressWLRefresh { refreshAll() } } }
    // A zero-width or negative window divides by zero in the extractor, so clamp
    // to >= 1 HU here — the TextField lets users type values the slider/stepper
    // can't, so the floor has to live in the model, not just the controls. The
    // re-entrant set settles immediately (1 is already >= 1, so no loop).
    @Published var window: Float = 400 {
        didSet {
            if window < 1 { window = 1; return }
            if !suppressWLRefresh { refreshAll() }
        }
    }
    private var suppressWLRefresh = false

    // Shared crosshair focus point in voxel coordinates (x,y,z). All three slice
    // planes pass through it, so clicking a point in one pane recenters the
    // others (3D-Slicer-style linked navigation).
    @Published var focus = SIMD3<Int>(0, 0, 0)
    @Published var images: [CGImage?] = [nil, nil, nil]

    // Display toggles for the slice overlays (crosshair/intersection lines and the
    // R/L/A/P/S/I orientation letters), persisted across launches in UserDefaults.
    @Published var showCrosshair: Bool = UserDefaults.standard.object(
        forKey: "showCrosshair") as? Bool ?? true {
        didSet { UserDefaults.standard.set(showCrosshair, forKey: "showCrosshair") }
    }
    @Published var showOrientationLabels: Bool = UserDefaults.standard.object(
        forKey: "showOrientationLabels") as? Bool ?? true {
        didSet { UserDefaults.standard.set(showOrientationLabels,
                                           forKey: "showOrientationLabels") }
    }

    // True only while the Shift key is held: a momentary cross-reference mode that
    // shows the crosshair and makes a hover recenter the other panes (Slicer's
    // Shift+move). Not persisted - it tracks a live key, not a preference.
    @Published var shiftActive = false
    func setShiftActive(_ active: Bool) {
        if shiftActive != active { shiftActive = active }
    }

    // Per-axis slice index derived from the focus voxel: axial steps Z, coronal
    // steps Y, sagittal steps X. Kept as an accessor so existing call sites and
    // the SlicePane sliders/labels keep working against the new source of truth.
    var sliceIndex: [Int] { [focus.z, focus.y, focus.x] }

    // Curated + full DICOM metadata for the loaded series (nil until a load).
    @Published var metadata: DicomMetadata?

    static let axisNames = ["Axial", "Coronal", "Sagittal"]

    func sliceCount(_ axis: Int) -> Int {
        guard let h = handle else { return 0 }
        return Int(lumen_slice_count(h, Int32(axis)))
    }

    // Physical width/height ratio of a plane, so anisotropic voxels (e.g. thick
    // CT slices) render with correct anatomical proportions rather than squished.
    func physicalAspect(_ axis: Int) -> CGFloat {
        let sx = CGFloat(spacing.x), sy = CGFloat(spacing.y), sz = CGFloat(spacing.z)
        let w = CGFloat(width), h = CGFloat(height), d = CGFloat(depth)
        let ratio: CGFloat
        switch axis {
        case 0: ratio = (w * sx) / (h * sy)   // Axial: X by Y
        case 1: ratio = (w * sx) / (d * sz)   // Coronal: X by Z
        default: ratio = (h * sy) / (d * sz)  // Sagittal: Y by Z
        }
        return ratio.isFinite && ratio > 0 ? ratio : 1
    }

    deinit {
        if let h = handle { lumen_free(h) }
        for h in deferredFree { lumen_free(h) }
        if let s = scanHandle { lumen_scan_free(s) }
    }

    // MARK: - Open flow (scan -> pick -> load)

    // Open a folder: scan its series first, then either load straight through (one
    // series) or hand a picker to the UI (several). This is the flow the Open Folder
    // button and drag-drop use. `load(path:)` below is the older single-shot
    // largest-series fallback, kept for the command-line auto-load.
    func open(path: String) {
        guard !isLoading else { return }
        isLoading = true
        status = "Scanning \(URL(fileURLWithPath: path).lastPathComponent)…"

        // Scan off the main thread (a header-only pass, but a big folder still walks
        // many files). The opaque scan handle rides back as a bit pattern to stay
        // clear of cross-actor Sendable concerns.
        Task.detached(priority: .userInitiated) {
            var msg = [CChar](repeating: 0, count: 512)
            let raw = path.withCString { cpath in
                UInt(bitPattern: lumen_scan_folder(cpath, &msg, 512))
            }
            let message = String(cString: msg)
            await MainActor.run {
                self.finishScan(scanBits: raw, message: message)
            }
        }
    }

    private func finishScan(scanBits: UInt, message: String) {
        // A fresh scan supersedes any earlier pending picker; never leak its handle.
        freeScan()
        pendingSeries = nil

        guard let scan = OpaquePointer(bitPattern: scanBits) else {
            // Scan found nothing DICOM-like: surface the reason, leave the volume be.
            isLoading = false
            status = message
            return
        }
        scanHandle = scan

        let count = Int(lumen_series_count(scan))
        if count <= 1 {
            // One series (or a lone unlabeled group): load it with no picker.
            loadChosenSeries(index: 0)
        } else {
            // Several series: read each one's details and let the UI pick. Keep the
            // scan handle alive until the user chooses or cancels.
            var options: [SeriesOption] = []
            options.reserveCapacity(count)
            for i in 0..<count { options.append(Self.readSeriesInfo(scan, index: i)) }
            pendingSeries = options
            isLoading = false
            status = "Multiple series found - choose one to load."
        }
    }

    /// Load the series the user picked from the live scan, then free the scan.
    func chooseSeries(_ index: Int) {
        pendingSeries = nil
        loadChosenSeries(index: index)
    }

    /// Dismiss the picker without loading: free the scan, no volume change. Guards on
    /// isLoading so it can't free a scan a chosen load is still reading (the sheet's
    /// dismissal setter fires this after chooseSeries has already started a load).
    func cancelSeries() {
        guard !isLoading else { return }
        guard pendingSeries != nil || scanHandle != nil else { return }
        pendingSeries = nil
        freeScan()
        if !hasVolume { status = "Open a DICOM folder to begin." }
    }

    // Load one series off the main thread, then adopt its handle and free the scan.
    private func loadChosenSeries(index: Int) {
        guard let scan = scanHandle else { return }
        let scanBits = UInt(bitPattern: scan)
        isLoading = true
        status = "Loading series…"
        Task.detached(priority: .userInitiated) {
            var msg = [CChar](repeating: 0, count: 512)
            let raw = UInt(bitPattern: lumen_load_series(
                OpaquePointer(bitPattern: scanBits), Int32(index), &msg, 512))
            let message = String(cString: msg)
            await MainActor.run {
                self.finishLoad(handleBits: raw, message: message)
                self.freeScan()
            }
        }
    }

    private func freeScan() {
        if let s = scanHandle { lumen_scan_free(s); scanHandle = nil }
    }

    // Read one series' description/modality/geometry/date out of the scan into a
    // value struct the picker can show without holding the C handle.
    private static func readSeriesInfo(_ scan: OpaquePointer, index: Int) -> SeriesOption {
        var desc = [CChar](repeating: 0, count: 256)
        var modality = [CChar](repeating: 0, count: 64)
        var created = [CChar](repeating: 0, count: 32)
        var sliceCount: Int32 = 0
        var width: Int32 = 0
        var height: Int32 = 0
        desc.withUnsafeMutableBufferPointer { d in
            modality.withUnsafeMutableBufferPointer { m in
                created.withUnsafeMutableBufferPointer { c in
                    lumen_series_info(scan, Int32(index), d.baseAddress, Int32(d.count),
                                      m.baseAddress, Int32(m.count), &sliceCount,
                                      &width, &height, c.baseAddress, Int32(c.count))
                }
            }
        }
        return SeriesOption(id: index,
                            description: String(cString: desc),
                            modality: String(cString: modality),
                            sliceCount: Int(sliceCount),
                            width: Int(width),
                            height: Int(height),
                            created: String(cString: created))
    }

    // The older single-shot open: load the largest series via lumen_load_folder with
    // no picker. Kept for the command-line auto-load; the UI uses open(path:).
    func load(path: String) {
        guard !isLoading else { return }
        isLoading = true
        status = "Loading \(URL(fileURLWithPath: path).lastPathComponent)…"

        // Parse off the main thread so the window/UI stay responsive - a real series
        // can be hundreds of files. The opaque handle is passed back as a bit pattern
        // to stay clear of cross-actor Sendable concerns.
        Task.detached(priority: .userInitiated) {
            var msg = [CChar](repeating: 0, count: 512)
            let raw = path.withCString { cpath in
                UInt(bitPattern: lumen_load_folder(cpath, &msg, 512))
            }
            let message = String(cString: msg)
            await MainActor.run {
                self.finishLoad(handleBits: raw, message: message)
            }
        }
    }

    private func finishLoad(handleBits: UInt, message: String) {
        isLoading = false
        status = message
        guard let newHandle = OpaquePointer(bitPattern: handleBits) else { return }
        adopt(handle: newHandle)
    }

    // Adopt a freshly loaded LumenVolume* as the current volume: retire the old handle
    // (deferring the free while a background mesh reader still holds a pin on it -
    // releaseHandle() frees it once that work finishes), publish geometry + HU range,
    // pick a default window/level, recenter the crosshair, and extract the first
    // slices. Shared by the folder fallback, the single-series path, and the
    // series-picker path so all three adopt a handle identically.
    private func adopt(handle newHandle: OpaquePointer) {
        if let old = handle {
            if pinnedReaders > 0 { deferredFree.append(old) } else { lumen_free(old) }
        }
        handle = newHandle
        hasVolume = true

        var w: Int32 = 0, h: Int32 = 0, d: Int32 = 0
        lumen_dims(newHandle, &w, &h, &d)
        width = Int(w); height = Int(h); depth = Int(d)

        var sx: Float = 0, sy: Float = 0, sz: Float = 0
        lumen_spacing(newHandle, &sx, &sy, &sz)
        spacing = SIMD3(sx, sy, sz)

        var lo: Float = 0, hi: Float = 0
        lumen_hu_range(newHandle, &lo, &hi)
        huLo = lo; huHi = hi

        // Default to the soft-tissue preset when the data spans it, else full range.
        if lo <= -200, hi >= 400 {
            level = 40; window = 400
        } else {
            level = (lo + hi) / 2
            window = max(1, hi - lo)
        }

        metadata = Self.readMetadata(newHandle)
        focus = SIMD3(width / 2, height / 2, depth / 2) // start centered
        refreshAll()
    }

    // Pull the serialized metadata blob from the bridge (two-call pattern: ask
    // for the length, then fill a right-sized buffer) and parse it.
    private static func readMetadata(_ handle: OpaquePointer) -> DicomMetadata? {
        let needed = Int(lumen_meta_json(handle, nil, 0))
        guard needed > 0 else { return nil }
        // `&buffer` would pass a pointer to the Array value, not its element
        // storage — withUnsafeMutableBufferPointer hands the C function a valid
        // UnsafeMutablePointer<CChar> into the contiguous backing buffer.
        var buffer = [CChar](repeating: 0, count: needed + 1)
        buffer.withUnsafeMutableBufferPointer { buf in
            _ = lumen_meta_json(handle, buf.baseAddress, Int32(buf.count))
        }
        return DicomMetadata.parse(String(cString: buffer))
    }

    func setSlice(_ axis: Int, _ value: Int) {
        switch axis {
        case 0: focus.z = clampZ(value)
        case 1: focus.y = clampY(value)
        default: focus.x = clampX(value)
        }
        // Coalesced, not synchronous: a fast scroll (or slider drag) fires many
        // setSlice calls, and re-extracting every intermediate slice inline is what
        // makes the sagittal pane feel choppy next to axial. Sagittal slices are the
        // slowest to extract — with the volume stored X-fastest, a constant-X plane
        // strides the whole buffer, roughly one cache miss per pixel — so paying that
        // for slices you scroll straight past is wasted. `focus` updates now (the
        // crosshair + slider track live); the image catches up on the next runloop
        // tick, dropping the slices in between.
        scheduleRefresh(axis)
    }

    // See setSlice: bump `focus` immediately, coalesce the (possibly slow) extraction
    // to the next runloop tick so a burst of slice changes collapses to one re-extract
    // of the latest slice.
    private var pendingRefresh: Set<Int> = []
    private var refreshScheduled = false
    private func scheduleRefresh(_ axis: Int) {
        pendingRefresh.insert(axis)
        guard !refreshScheduled else { return }
        refreshScheduled = true
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.refreshScheduled = false
            let axes = self.pendingRefresh
            self.pendingRefresh.removeAll()
            for a in axes { self.refresh(a) }
        }
    }

    /// Jump the shared focus to a voxel (click-to-locate, or Shift+hover linked
    /// navigation). Recenters all three panes so they pass through the point. Only
    /// the planes whose slice index actually moved are re-extracted - the others
    /// just redraw their crosshair from the published `focus`. That keeps a
    /// continuous Shift-hover (one jump per mouse-move) cheap: the hovered pane's
    /// own slice never changes, so at most the other two re-extract.
    func jump(to voxel: SIMD3<Int>) {
        let next = SIMD3(clampX(voxel.x), clampY(voxel.y), clampZ(voxel.z))
        let prev = focus
        guard next != prev else { return }
        focus = next
        if next.z != prev.z { refresh(0) }
        if next.y != prev.y { refresh(1) }
        if next.x != prev.x { refresh(2) }
    }

    private func clampX(_ v: Int) -> Int { min(max(v, 0), max(width - 1, 0)) }
    private func clampY(_ v: Int) -> Int { min(max(v, 0), max(height - 1, 0)) }
    private func clampZ(_ v: Int) -> Int { min(max(v, 0), max(depth - 1, 0)) }

    // MARK: - Slice geometry seam
    //
    // These two methods are the ONLY place pane-pixel <-> voxel geometry is
    // resolved (they delegate to the C++ orthogonal plane_map, the single source
    // of truth). An oblique/RAS model would replace just these — see
    // yashdocs/slicer-parity/PLAN.md.

    /// Voxel under image pixel (px,py) of the current slice on `axis`.
    func voxel(onAxis axis: Int, px: Int, py: Int) -> SIMD3<Int>? {
        guard let h = handle else { return nil }
        var x: Int32 = 0, y: Int32 = 0, z: Int32 = 0
        lumen_slice_pixel_to_voxel(h, Int32(axis), Int32(sliceIndex[axis]),
                                   Int32(px), Int32(py), &x, &y, &z)
        return SIMD3(Int(x), Int(y), Int(z))
    }

    /// Where an arbitrary voxel projects onto pane `axis` (for markup dots).
    func slicePixel(onAxis axis: Int, voxel v: SIMD3<Int>) -> (px: Int, py: Int)? {
        guard let h = handle else { return nil }
        var px: Int32 = 0, py: Int32 = 0
        lumen_voxel_to_slice_pixel(h, Int32(axis), Int32(v.x), Int32(v.y), Int32(v.z),
                                   &px, &py)
        return (Int(px), Int(py))
    }

    /// Where the shared focus voxel projects onto pane `axis` (for the crosshair).
    func crosshairPixel(onAxis axis: Int) -> (px: Int, py: Int)? {
        guard let h = handle else { return nil }
        var px: Int32 = 0, py: Int32 = 0
        lumen_voxel_to_slice_pixel(h, Int32(axis), Int32(focus.x), Int32(focus.y),
                                   Int32(focus.z), &px, &py)
        return (Int(px), Int(py))
    }

    /// Set window and level together with a single re-render. Clamps window to
    /// at least 1 HU (a zero-width window divides by zero in the extractor).
    /// Used by presets and the drag-on-image gesture so they don't double-refresh.
    func setWindowLevel(level newLevel: Float, window newWindow: Float) {
        suppressWLRefresh = true
        window = max(1, newWindow)
        level = newLevel
        suppressWLRefresh = false
        refreshAll()
    }

    func refreshAll() {
        guard hasVolume else { return }
        for axis in 0..<3 { refresh(axis) }
    }

    private func refresh(_ axis: Int) {
        guard let h = handle else { images[axis] = nil; return }
        var w: Int32 = 0, ht: Int32 = 0
        guard let ptr = lumen_extract_slice(h, Int32(axis), Int32(sliceIndex[axis]),
                                            level, window, &w, &ht),
              w > 0, ht > 0 else {
            images[axis] = nil
            return
        }
        let count = Int(w) * Int(ht) * 4
        let data = Data(bytes: ptr, count: count) // copies out of the scratch buffer
        images[axis] = Self.makeImage(data: data, width: Int(w), height: Int(ht))
    }

    // Wrap raw premultiplied-RGBA8 bytes in a CGImage. Shared with the
    // SegmentationModel for mask overlays (same pixel format).
    nonisolated static func makeImage(data: Data, width: Int, height: Int) -> CGImage? {
        guard let provider = CGDataProvider(data: data as CFData) else { return nil }
        let bitmapInfo = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue)
        return CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: width * 4,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: bitmapInfo,
            provider: provider,
            decode: nil,
            shouldInterpolate: false,
            intent: .defaultIntent
        )
    }
}
