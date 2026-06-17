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
    static func pixel(forTap tap: CGPoint,
                      container: CGSize,
                      imageWidth: Int,
                      imageHeight: Int,
                      aspect: CGFloat,
                      padding: CGFloat = 8) -> (x: Int, y: Int)? {
        guard imageWidth > 0, imageHeight > 0, aspect > 0 else { return nil }
        let availW = container.width - 2 * padding
        let availH = container.height - 2 * padding
        guard availW > 0, availH > 0 else { return nil }

        // Fit a rect of `aspect` (display ratio) inside the available area.
        let fittedW: CGFloat
        let fittedH: CGFloat
        if availW / availH > aspect {
            fittedH = availH
            fittedW = availH * aspect
        } else {
            fittedW = availW
            fittedH = availW / aspect
        }
        let originX = padding + (availW - fittedW) / 2
        let originY = padding + (availH - fittedH) / 2

        let u = (tap.x - originX) / fittedW
        let v = (tap.y - originY) / fittedH
        guard u >= 0, u <= 1, v >= 0, v <= 1 else { return nil }

        // Normalised position maps linearly onto the full pixel grid regardless of
        // the display aspect (the pixels are stretched uniformly into `fitted`).
        let px = min(imageWidth - 1, max(0, Int(u * CGFloat(imageWidth))))
        let py = min(imageHeight - 1, max(0, Int(v * CGFloat(imageHeight))))
        return (px, py)
    }
}
