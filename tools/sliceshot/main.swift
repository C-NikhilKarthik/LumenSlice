// SliceShot — render the three center slices of a DICOM folder to a PNG.
//
// Uses the same C bridge + window/level + CGImage path as the SwiftUI app, so it
// is a faithful, headless preview of what the app shows (handy for CI snapshots
// and for verifying a dataset without opening the GUI).
//
//   swift run SliceShot <dicom_folder> <output.png>

import Foundation
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers
import LumenCore

func die(_ msg: String) -> Never {
    FileHandle.standardError.write((msg + "\n").data(using: .utf8)!)
    exit(1)
}

let args = CommandLine.arguments
guard args.count >= 3 else { die("usage: SliceShot <dicom_folder> <output.png>") }
let folder = args[1], outPath = args[2]

var msg = [CChar](repeating: 0, count: 512)
guard let vol = folder.withCString({ lumen_load_folder($0, &msg, 512) }) else {
    die("load failed: \(String(cString: msg))")
}
print(String(cString: msg))

var w: Int32 = 0, h: Int32 = 0, d: Int32 = 0
lumen_dims(vol, &w, &h, &d)
var lo: Float = 0, hi: Float = 0
lumen_hu_range(vol, &lo, &hi)
// Match the app's default windowing.
let level: Float, window: Float
if lo <= -200, hi >= 400 { level = 40; window = 400 }
else { level = (lo + hi) / 2; window = max(1, hi - lo) }

func sliceImage(_ axis: Int32) -> CGImage? {
    let count = lumen_slice_count(vol, axis)
    var sw: Int32 = 0, sh: Int32 = 0
    guard let ptr = lumen_extract_slice(vol, axis, count / 2, level, window, &sw, &sh),
          sw > 0, sh > 0 else { return nil }
    let data = Data(bytes: ptr, count: Int(sw) * Int(sh) * 4)
    guard let provider = CGDataProvider(data: data as CFData) else { return nil }
    return CGImage(width: Int(sw), height: Int(sh), bitsPerComponent: 8, bitsPerPixel: 32,
                   bytesPerRow: Int(sw) * 4, space: CGColorSpaceCreateDeviceRGB(),
                   bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                   provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
}

let slices = [sliceImage(0), sliceImage(1), sliceImage(2)]
let labels = ["Axial", "Coronal", "Sagittal"]

// Composite the three panes side by side on a dark canvas.
let pane = 360, gap = 16, pad = 24, labelH = 28
let canvasW = pad * 2 + pane * 3 + gap * 2
let canvasH = pad * 2 + labelH + pane
guard let ctx = CGContext(data: nil, width: canvasW, height: canvasH, bitsPerComponent: 8,
                          bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(),
                          bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
    die("could not create canvas")
}
ctx.setFillColor(CGColor(red: 0.09, green: 0.10, blue: 0.11, alpha: 1))
ctx.fill(CGRect(x: 0, y: 0, width: canvasW, height: canvasH))
ctx.interpolationQuality = .none

for (i, img) in slices.enumerated() {
    let x = pad + i * (pane + gap)
    let y = pad
    ctx.setFillColor(CGColor(red: 0, green: 0, blue: 0, alpha: 1))
    ctx.fill(CGRect(x: x, y: y, width: pane, height: pane))
    guard let img else { continue }
    // Fit preserving aspect.
    let ar = CGFloat(img.width) / CGFloat(img.height)
    var dw = CGFloat(pane), dh = CGFloat(pane)
    if ar > 1 { dh = dw / ar } else { dw = dh * ar }
    let ox = CGFloat(x) + (CGFloat(pane) - dw) / 2
    let oy = CGFloat(y) + (CGFloat(pane) - dh) / 2
    ctx.draw(img, in: CGRect(x: ox, y: oy, width: dw, height: dh))
    print("  \(labels[i]): \(img.width)x\(img.height)")
}

guard let composite = ctx.makeImage() else { die("composite failed") }
let url = URL(fileURLWithPath: outPath)
guard let dest = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil) else {
    die("could not open \(outPath)")
}
CGImageDestinationAddImage(dest, composite, nil)
guard CGImageDestinationFinalize(dest) else { die("write failed") }
lumen_free(vol)
print("wrote \(outPath)  (\(canvasW)x\(canvasH))")
