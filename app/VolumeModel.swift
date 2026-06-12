import SwiftUI
import LumenCore

// Observable bridge between SwiftUI and the C++ core. Holds the loaded volume
// handle and republishes CGImages for the three slice planes whenever the user
// scrolls or adjusts window/level.
@MainActor
final class VolumeModel: ObservableObject {
    private var handle: OpaquePointer?

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
    @Published var window: Float = 400 { didSet { if !suppressWLRefresh { refreshAll() } } }
    private var suppressWLRefresh = false

    // Per-axis scroll position and rendered slice.
    @Published var sliceIndex: [Int] = [0, 0, 0]
    @Published var images: [CGImage?] = [nil, nil, nil]

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
        sliceIndex = [sliceCount(0) / 2, sliceCount(1) / 2, sliceCount(2) / 2]
        refreshAll()
    }

    // Pull the serialized metadata blob from the bridge (two-call pattern: ask
    // for the length, then fill a right-sized buffer) and parse it.
    private static func readMetadata(_ handle: OpaquePointer) -> DicomMetadata? {
        let needed = Int(lumen_meta_json(handle, nil, 0))
        guard needed > 0 else { return nil }
        var buffer = [CChar](repeating: 0, count: needed + 1)
        _ = lumen_meta_json(handle, &buffer, Int32(needed + 1))
        return DicomMetadata.parse(String(cString: buffer))
    }

    func setSlice(_ axis: Int, _ value: Int) {
        sliceIndex[axis] = value
        refresh(axis)
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

    private static func makeImage(data: Data, width: Int, height: Int) -> CGImage? {
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
