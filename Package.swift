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
                .unsafeFlags(["-I\(dcmtkInclude)"]),
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
        // Headless ingestion smoke test (no window / GPU).
        .executableTarget(
            name: "IngestTest",
            dependencies: ["LumenCore"],
            path: "tests"
        ),
        // Headless slice-preview renderer (writes a PNG of the 3 center slices).
        .executableTarget(
            name: "SliceShot",
            dependencies: ["LumenCore"],
            path: "tools/sliceshot"
        ),
    ],
    cxxLanguageStandard: .cxx17
)
