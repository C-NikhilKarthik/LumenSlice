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
                ]),
            ],
            linkerSettings: [
                // Link DCMTK *statically* so the produced binary has no Homebrew
                // dependency and can be shipped in a self-contained .app bundle.
                // Only system libraries (libz, libc++, libSystem) remain.
                .unsafeFlags([
                    "\(dcmtkLib)/libdcmdata.a",
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
        // Headless slice-preview renderer (writes a PNG of the 3 center slices).
        .executableTarget(
            name: "SliceShot",
            dependencies: ["LumenCore"],
            path: "tools/sliceshot"
        ),
    ],
    cxxLanguageStandard: .cxx20
)
