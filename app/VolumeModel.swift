import SwiftUI
import LumenCore

// Observable bridge between SwiftUI and the C++ core. Holds the loaded volume
// handle and republishes CGImages for the three slice planes whenever the user
// scrolls or adjusts window/level.
@MainActor
final class VolumeModel: ObservableObject {
    // Readable by the SegmentationModel / MeshModel (same module), which drive the
    // same C++ volume handle; only VolumeModel mutates it (load/free).
    private(set) var handle: OpaquePointer?

    @Published var status = "Open a DICOM folder to begin."
    @Published var hasVolume = false
    @Published var isLoading = false

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
    }

    func load(path: String) {
        guard !isLoading else { return }
        isLoading = true
        status = "Loading \(URL(fileURLWithPath: path).lastPathComponent)…"

        // Parse off the main thread so the window/UI stay responsive — a real
        // series can be hundreds of files. The opaque handle is passed back as a
        // bit pattern to stay clear of cross-actor Sendable concerns.
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

        if let old = handle { lumen_free(old) }
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
        refresh(axis)
    }

    /// Jump the shared focus to a voxel (click-to-locate). Recenters all three
    /// panes so they pass through the clicked anatomical point.
    func jump(to voxel: SIMD3<Int>) {
        focus = SIMD3(clampX(voxel.x), clampY(voxel.y), clampZ(voxel.z))
        refreshAll()
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
    static func makeImage(data: Data, width: Int, height: Int) -> CGImage? {
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
