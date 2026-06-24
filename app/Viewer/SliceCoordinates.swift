import CoreGraphics

// Maps a tap inside a SlicePane's image area to an image pixel. This is the Swift
// half of the silent-failure path the eng review flagged: a wrong mapping seeds /
// paints the wrong voxel with no error. It mirrors SlicePane's layout exactly —
// the HU image is fit (aspect-correct, letterboxed) inside the content area inset
// by `padding` on every side — and is pure so it can be unit-tested without UI.
//
//   container (the pane content box)
//   ┌───────────────────────────────┐
//   │  padding                       │
//   │   ┌───────────────────────┐    │   fitted = the aspect-correct rect the
//   │   │      fitted image      │   │   image actually occupies; taps outside
//   │   └───────────────────────┘    │   it (the letterbox margin) return nil.
//   │                                │
//   └───────────────────────────────┘
enum SliceCoordinates {
    // The aspect-correct rect the slice image actually occupies inside the pane
    // content box (the area inside `padding`). Returns nil if the box is empty.
    static func fittedRect(container: CGSize, aspect: CGFloat,
                           padding: CGFloat = 8) -> CGRect? {
        guard aspect > 0 else { return nil }
        let availW = container.width - 2 * padding
        let availH = container.height - 2 * padding
        guard availW > 0, availH > 0 else { return nil }
        let fittedW: CGFloat
        let fittedH: CGFloat
        if availW / availH > aspect {
            fittedH = availH
            fittedW = availH * aspect
        } else {
            fittedW = availW
            fittedH = availW / aspect
        }
        return CGRect(x: padding + (availW - fittedW) / 2,
                      y: padding + (availH - fittedH) / 2,
                      width: fittedW, height: fittedH)
    }

    // Tap point -> image pixel, or nil if the tap is in the letterbox margin.
    static func pixel(forTap tap: CGPoint,
                      container: CGSize,
                      imageWidth: Int,
                      imageHeight: Int,
                      aspect: CGFloat,
                      padding: CGFloat = 8) -> (x: Int, y: Int)? {
        guard imageWidth > 0, imageHeight > 0,
              let rect = fittedRect(container: container, aspect: aspect,
                                    padding: padding) else { return nil }
        let u = (tap.x - rect.minX) / rect.width
        let v = (tap.y - rect.minY) / rect.height
        guard u >= 0, u <= 1, v >= 0, v <= 1 else { return nil }
        let px = min(imageWidth - 1, max(0, Int(u * CGFloat(imageWidth))))
        let py = min(imageHeight - 1, max(0, Int(v * CGFloat(imageHeight))))
        return (px, py)
    }

    // Inverse: the center of image pixel (px,py) in pane coordinates, for drawing
    // the crosshair / intersection lines. nil if the pane box is empty.
    static func point(forPixel px: Int, _ py: Int,
                      container: CGSize,
                      imageWidth: Int,
                      imageHeight: Int,
                      aspect: CGFloat,
                      padding: CGFloat = 8) -> CGPoint? {
        guard imageWidth > 0, imageHeight > 0,
              let rect = fittedRect(container: container, aspect: aspect,
                                    padding: padding) else { return nil }
        let u = (CGFloat(px) + 0.5) / CGFloat(imageWidth)
        let v = (CGFloat(py) + 0.5) / CGFloat(imageHeight)
        return CGPoint(x: rect.minX + u * rect.width,
                       y: rect.minY + v * rect.height)
    }
}
