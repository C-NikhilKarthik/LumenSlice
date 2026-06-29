// swift-tools-version:5.9
import PackageDescription

// DCMTK is resolved from the Homebrew install (see docs/dependencies.md).
let dcmtkInclude = "/opt/homebrew/opt/dcmtk/include"
let dcmtkLib = "/opt/homebrew/opt/dcmtk/lib"

let package = Package(
    name: "LumenSlice",
    platforms: [.macOS(.v13)],
    targets: [
        // C++ core (data-oriented, UI-agnostic) + the C bridge Swift talks to.
        .target(
            name: "LumenCore",
            path: "src",
            publicHeadersPath: "bridge/include",
            cxxSettings: [
                .headerSearchPath("."), // so "core/volume.h" etc. resolve
                .unsafeFlags([
                    "-I\(dcmtkInclude)",
                    // Strong warnings on our translation units. We hold -Werror
                    // and -Wconversion because our .cpp files #include DCMTK
                    // headers, whose own inline code trips those flags; our code
                    // stays clean under these and is reviewed against cpp.md.
                    "-Wall", "-Wextra", "-Wpedantic",
                    // Always optimize the numeric core, even in debug (swift run).
                    // It is voxel-heavy, data-oriented C++ with no Swift to step
                    // through; at -O0 marching cubes / mask scans run ~15-20x
                    // slower, which is the difference between a snappy 3D generate
                    // and a multi-second (large-volume: minute-plus) stall during
                    // dev iteration. Release already optimizes; this lifts debug.
                    "-O2",
                ]),
            ],
            linkerSettings: [
                // Link DCMTK *statically* so the produced binary has no Homebrew
                // dependency and can be shipped in a self-contained .app bundle.
                // Only system libraries (libz, libc++, libSystem) remain.
                .unsafeFlags([
                    // Decoders for encapsulated (compressed) pixel data so real-world
                    // JPEG / JPEG-LS / RLE DICOM series load, not just native LE.
                    // Listed dependents-first (static link order): the dcm* codec
                    // libs reference symbols resolved by dcmdata + the IJG/CharLS
                    // backends that follow.
                    "\(dcmtkLib)/libdcmjpls.a",
                    "\(dcmtkLib)/libdcmjpeg.a",
                    "\(dcmtkLib)/libdcmimage.a",
                    "\(dcmtkLib)/libdcmimgle.a",
                    "\(dcmtkLib)/libdcmdata.a",
                    "\(dcmtkLib)/libijg16.a",
                    "\(dcmtkLib)/libijg12.a",
                    "\(dcmtkLib)/libijg8.a",
                    "\(dcmtkLib)/libdcmtkcharls.a",
                    "\(dcmtkLib)/liboflog.a",
                    "\(dcmtkLib)/libofstd.a",
                    "\(dcmtkLib)/liboficonv.a",
                    "-lz",
                ]),
            ]
        ),
        // The SwiftUI macOS app.
        .executableTarget(
            name: "LumenSlice",
            dependencies: ["LumenCore"],
            path: "app"
        ),
        // Headless ingestion smoke test (no window / GPU). Sources are pinned so
        // this target does not glob the Swift unit tests that also live under tests/.
        .executableTarget(
            name: "IngestTest",
            dependencies: ["LumenCore"],
            path: "tests",
            sources: ["ingest_test.cpp"]
        ),
        // Unit tests for the Swift app logic (W/L math, metadata parsing). Run
        // with `swift test`. Depends on the app target to reach its types.
        .testTarget(
            name: "LumenSliceTests",
            dependencies: ["LumenCore", "LumenSlice"],
            path: "tests/unit"
        ),
        // C++ unit test for the metadata serializer's JSON escaping. Self-
        // contained (no DICOM file). Run with `swift run MetaTest`.
        .executableTarget(
            name: "MetaTest",
            dependencies: ["LumenCore"],
            path: "tests/cpp",
            sources: ["meta_test.cpp"],
            cxxSettings: [.headerSearchPath("../../src")]
        ),
        // C++ unit tests for the segmentation core (plane_map round-trip,
        // threshold, region grow, paint, overlay). Run with `swift run SegTest`.
        .executableTarget(
            name: "SegTest",
            dependencies: ["LumenCore"],
            path: "tests/cpp",
            sources: ["seg_test.cpp"],
            cxxSettings: [.headerSearchPath("../../src")]
        ),
        // Headless slice-preview renderer (writes a PNG of the 3 center slices).
        .executableTarget(
            name: "SliceShot",
            dependencies: ["LumenCore"],
            path: "tools/sliceshot"
        ),
        // Headless 3D pipeline check (threshold -> marching cubes -> binary STL).
        .executableTarget(
            name: "MeshShot",
            dependencies: ["LumenCore"],
            path: "tools/meshshot"
        ),
    ],
    cxxLanguageStandard: .cxx20
)
