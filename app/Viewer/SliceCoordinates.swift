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
    // The display rect the slice image occupies inside the pane content box. At
    // zoom == 1 this is the aspect-correct fit (letterboxed inside `padding`); a
    // higher zoom scales that rect about `anchor` (the cursor point where the
    // right-drag began), so the area under the cursor stays put while the image
    // grows. Because rendering, the crosshair, the brush, AND hit-testing all read
    // this one rect, a zoomed pane keeps painting/locating the correct voxel.
    // Returns nil if the box is empty.
    static func fittedRect(container: CGSize, aspect: CGFloat,
                           padding: CGFloat = 8,
                           zoom: CGFloat = 1, anchor: CGPoint = .zero,
                           pan: CGSize = .zero) -> CGRect? {
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
        let base = CGRect(x: padding + (availW - fittedW) / 2,
                          y: padding + (availH - fittedH) / 2,
                          width: fittedW, height: fittedH)
        let scaled = zoom == 1 ? base : zoomed(base, scale: zoom, anchor: anchor)
        return (pan == .zero) ? scaled
                              : scaled.offsetBy(dx: pan.width, dy: pan.height)
    }

    // Scale `base` by `scale` about `anchor`: the anchor point maps to itself, so
    // zooming keeps whatever sits under the cursor fixed in place.
    static func zoomed(_ base: CGRect, scale: CGFloat, anchor: CGPoint) -> CGRect {
        CGRect(x: anchor.x + (base.minX - anchor.x) * scale,
               y: anchor.y + (base.minY - anchor.y) * scale,
               width: base.width * scale, height: base.height * scale)
    }

    // Tap point -> image pixel, or nil if the tap is in the letterbox margin.
    static func pixel(forTap tap: CGPoint,
                      container: CGSize,
                      imageWidth: Int,
                      imageHeight: Int,
                      aspect: CGFloat,
                      padding: CGFloat = 8,
                      zoom: CGFloat = 1, anchor: CGPoint = .zero,
                      pan: CGSize = .zero) -> (x: Int, y: Int)? {
        guard imageWidth > 0, imageHeight > 0,
              let rect = fittedRect(container: container, aspect: aspect,
                                    padding: padding, zoom: zoom,
                                    anchor: anchor, pan: pan) else { return nil }
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
                      padding: CGFloat = 8,
                      zoom: CGFloat = 1, anchor: CGPoint = .zero,
                      pan: CGSize = .zero) -> CGPoint? {
        guard imageWidth > 0, imageHeight > 0,
              let rect = fittedRect(container: container, aspect: aspect,
                                    padding: padding, zoom: zoom,
                                    anchor: anchor, pan: pan) else { return nil }
        let u = (CGFloat(px) + 0.5) / CGFloat(imageWidth)
        let v = (CGFloat(py) + 0.5) / CGFloat(imageHeight)
        return CGPoint(x: rect.minX + u * rect.width,
                       y: rect.minY + v * rect.height)
    }
}
