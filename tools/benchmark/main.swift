// LumenBench — headless baseline timings for the shared C bridge.
//
//   swift run LumenBench <dicom_folder> [iterations] [lowHU] [highHU]

import Foundation
import LumenCore

func fail(_ message: String) -> Never {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
    exit(1)
}

let args = CommandLine.arguments
guard args.count >= 2 else {
    fail("usage: LumenBench <dicom_folder> [iterations] [lowHU] [highHU]")
}

let folder = args[1]
let iterations = max(1, Int(args.count > 2 ? args[2] : "3") ?? 3)
let lowHU = Float(args.count > 3 ? args[3] : "300") ?? 300
let highHU = Float(args.count > 4 ? args[4] : "3000") ?? 3000

var message = [CChar](repeating: 0, count: 512)
guard let volume = folder.withCString({ lumen_load_folder($0, &message, 512) }) else {
    fail("load failed: \(String(cString: message))")
}
defer { lumen_free(volume) }

var width: Int32 = 0
var height: Int32 = 0
var depth: Int32 = 0
lumen_dims(volume, &width, &height, &depth)

var sx: Float = 0
var sy: Float = 0
var sz: Float = 0
lumen_spacing(volume, &sx, &sy, &sz)

print("volume=\(width)x\(height)x\(depth) spacing=\(sx)/\(sy)/\(sz)")
print("iterations=\(iterations) threshold=\(lowHU)..\(highHU)")

@inline(__always)
func elapsedMilliseconds(_ work: () -> Void) -> Double {
    let start = DispatchTime.now().uptimeNanoseconds
    work()
    let end = DispatchTime.now().uptimeNanoseconds
    return Double(end - start) / 1_000_000.0
}

func average(_ values: [Double]) -> Double {
    values.reduce(0, +) / Double(max(values.count, 1))
}

var loadMessage = String(cString: message)
if !loadMessage.isEmpty { print("loader=\(loadMessage)") }

var sliceTimes: [Double] = []
var cachedSliceTimes: [Double] = []
var overlayTimes: [Double] = []
var cachedOverlayTimes: [Double] = []
var volumeTextureTimes: [Double] = []
var cachedVolumeTextureTimes: [Double] = []
var thresholdTimes: [Double] = []
var meshSnapshotTimes: [Double] = []
var meshGenerateTimes: [Double] = []
var lastTriangles: Int32 = 0

for _ in 0..<iterations {
    var sliceTime = 0.0
    for axis in Int32(0)..<Int32(3) {
        var count = lumen_slice_count(volume, axis)
        if count <= 0 { count = 1 }
        var outWidth: Int32 = 0
        var outHeight: Int32 = 0
        sliceTime += elapsedMilliseconds {
            _ = lumen_extract_slice(volume, axis, count / 2, 40, 400,
                                    &outWidth, &outHeight)
        }
        cachedSliceTimes.append(elapsedMilliseconds {
            _ = lumen_extract_slice(volume, axis, count / 2, 40, 400,
                                    &outWidth, &outHeight)
        })
    }
    sliceTimes.append(sliceTime)

    lumen_seg_set_active(volume, 1)
    thresholdTimes.append(elapsedMilliseconds {
        lumen_seg_threshold(volume, lowHU, highHU)
    })

    var maskWidth: Int32 = 0
    var maskHeight: Int32 = 0
    overlayTimes.append(elapsedMilliseconds {
        _ = lumen_extract_mask_slice(volume, Int32(LUMEN_AXIS_AXIAL),
                                     depth / 2, &maskWidth, &maskHeight)
    })
    cachedOverlayTimes.append(elapsedMilliseconds {
        _ = lumen_extract_mask_slice(volume, Int32(LUMEN_AXIS_AXIAL),
                                     depth / 2, &maskWidth, &maskHeight)
    })

    var textureWidth: Int32 = 0
    var textureHeight: Int32 = 0
    var textureDepth: Int32 = 0
    volumeTextureTimes.append(elapsedMilliseconds {
        _ = lumen_extract_volume_texture(volume, 40, 400, 256,
                                         &textureWidth, &textureHeight,
                                         &textureDepth)
    })
    cachedVolumeTextureTimes.append(elapsedMilliseconds {
        _ = lumen_extract_volume_texture(volume, 40, 400, 256,
                                         &textureWidth, &textureHeight,
                                         &textureDepth)
    })

    meshSnapshotTimes.append(elapsedMilliseconds {
        lumen_mesh_snapshot(volume)
    })
    meshGenerateTimes.append(elapsedMilliseconds {
        lastTriangles = lumen_mesh_generate(volume, 1, 1)
    })
}

print(String(format: "slice_extract_all_axes_ms=%.3f", average(sliceTimes)))
print(String(format: "slice_extract_cached_ms=%.3f", average(cachedSliceTimes)))
print(String(format: "threshold_ms=%.3f", average(thresholdTimes)))
print(String(format: "mask_overlay_ms=%.3f", average(overlayTimes)))
print(String(format: "mask_overlay_cached_ms=%.3f", average(cachedOverlayTimes)))
print(String(format: "volume_texture_ms=%.3f", average(volumeTextureTimes)))
print(String(format: "volume_texture_cached_ms=%.3f", average(cachedVolumeTextureTimes)))
print(String(format: "mesh_snapshot_ms=%.3f", average(meshSnapshotTimes)))
print(String(format: "mesh_generate_ms=%.3f", average(meshGenerateTimes)))
print("labelled_voxels=\(lumen_seg_count(volume)) triangles=\(lastTriangles)")
