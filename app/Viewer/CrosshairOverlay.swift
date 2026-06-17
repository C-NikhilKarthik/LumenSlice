import SwiftUI

// 3D-Slicer-style colored slice colors: Red = Axial, Green = Coronal,
// Yellow = Sagittal. In each pane the crosshair's two lines ARE the intersections
// of the other two planes, so we color each line by the plane it represents.
enum PlaneColors {
    static let axial = Color.red
    static let coronal = Color.green
    static let sagittal = Color.yellow

    // (vertical line color, horizontal line color) for pane `axis`.
    static func forPane(_ axis: Int) -> (vertical: Color, horizontal: Color) {
        switch axis {
        case 0: return (sagittal, coronal) // axial: vert=x (sagittal), horiz=y (coronal)
        case 1: return (sagittal, axial)   // coronal: vert=x (sagittal), horiz=z (axial)
        default: return (coronal, axial)   // sagittal: vert=y (coronal), horiz=z (axial)
        }
    }
}

// Draws the crosshair / slice-intersection lines at the shared focus point,
// clipped to the fitted image rect, with a small gap at the center so the point
// stays visible. Non-interactive.
struct CrosshairOverlay: View {
    let point: CGPoint?
    let rect: CGRect?
    let verticalColor: Color
    let horizontalColor: Color

    var body: some View {
        Canvas { ctx, _ in
            guard let p = point, let r = rect, r.contains(p) else { return }
            let gap: CGFloat = 5

            var vertical = Path()
            vertical.move(to: CGPoint(x: p.x, y: r.minY))
            vertical.addLine(to: CGPoint(x: p.x, y: p.y - gap))
            vertical.move(to: CGPoint(x: p.x, y: p.y + gap))
            vertical.addLine(to: CGPoint(x: p.x, y: r.maxY))
            ctx.stroke(vertical, with: .color(verticalColor.opacity(0.9)), lineWidth: 1)

            var horizontal = Path()
            horizontal.move(to: CGPoint(x: r.minX, y: p.y))
            horizontal.addLine(to: CGPoint(x: p.x - gap, y: p.y))
            horizontal.move(to: CGPoint(x: p.x + gap, y: p.y))
            horizontal.addLine(to: CGPoint(x: r.maxX, y: p.y))
            ctx.stroke(horizontal, with: .color(horizontalColor.opacity(0.9)), lineWidth: 1)
        }
        .allowsHitTesting(false)
    }
}
