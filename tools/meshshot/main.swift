// MeshShot — headless 3D pipeline check: load a DICOM folder, threshold a
// structure, run marching cubes, and write a binary STL. Exercises the exact C
// bridge calls the app's Segment + 3D tabs make, so it is a faithful end-to-end
// test of the segmentation -> mesh -> STL path without the GUI (handy for CI).
//
//   swift run MeshShot <dicom_folder> <output.stl> [lowHU] [highHU]

import Foundation
import LumenCore

func die(_ msg: String) -> Never {
    FileHandle.standardError.write((msg + "\n").data(using: .utf8)!)
    exit(1)
}

let args = CommandLine.arguments
guard args.count >= 3 else { die("usage: MeshShot <dicom_folder> <output.stl> [lowHU] [highHU]") }
let folder = args[1], outPath = args[2]
let lowHU = args.count > 3 ? Float(args[3]) ?? 300 : 300
let highHU = args.count > 4 ? Float(args[4]) ?? 3000 : 3000

var msg = [CChar](repeating: 0, count: 512)
guard let vol = folder.withCString({ lumen_load_folder($0, &msg, 512) }) else {
    die("load failed: \(String(cString: msg))")
}
print(String(cString: msg))

// Segment: threshold the requested HU band (default bone).
lumen_seg_threshold(vol, lowHU, highHU)
let voxels = lumen_seg_count(vol)
print("threshold [\(Int(lowHU))..\(Int(highHU))] HU -> \(voxels) voxels")
guard voxels > 0 else { die("no voxels in HU range; nothing to mesh") }

// 3D: snapshot (main-thread step) then generate (the background step).
lumen_mesh_snapshot(vol)
let tris = lumen_mesh_generate(vol, 1, 1) // smoothing=1, full resolution
let verts = lumen_mesh_vertex_count(vol)
print("marching cubes -> \(tris) triangles, \(verts) vertices")
guard tris > 0 else { die("marching cubes produced no triangles") }

// Export STL.
let rc = outPath.withCString { lumen_mesh_write_stl(vol, $0) }
guard rc == 0 else { die("STL write failed (code \(rc))") }
lumen_free(vol)

let size = (try? FileManager.default.attributesOfItem(atPath: outPath)[.size] as? Int) ?? nil
print("wrote \(outPath)  (\(size ?? -1) bytes; expected \(84 + 50 * Int(tris)))")
