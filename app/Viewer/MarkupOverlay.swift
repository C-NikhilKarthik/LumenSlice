import SwiftUI

// Draws the markups on one slice pane. Each markup point is projected to a display
// point by `project` (voxel -> pane point, honouring zoom/pan); points that lie on
// the current slice are drawn as filled dots, off-slice points as hollow rings, so
// a markup reads as "here" vs "on another slice". Connected types (line/curve/
// angle/plane) also stroke their path, and line/curve show a length while angle
// shows its degrees. Observes the model so placing a point redraws immediately.
struct MarkupOverlay: View {
    @ObservedObject var markups: MarkupsModel
    let project: (SIMD3<Int>) -> CGPoint?
    let onSlice: (SIMD3<Int>) -> Bool

    var body: some View {
        Canvas { ctx, _ in
            for markup in markups.markups where markup.visible {
                draw(markup, in: &ctx)
            }
        }
        .allowsHitTesting(false)
    }

    private func draw(_ markup: Markup, in ctx: inout GraphicsContext) {
        let pts = markup.points.compactMap { project($0) }
        guard !pts.isEmpty else { return }
        let color = markup.color
        let isActive = markup.id == markups.activeID
        let lineWidth: CGFloat = isActive ? 2 : 1.2

        // Connecting path (for everything but a loose point list).
        if markup.type.isConnected, pts.count >= 2 {
            var path = Path()
            path.move(to: pts[0])
            for p in pts.dropFirst() { path.addLine(to: p) }
            if markup.type.isClosed || markup.type == .plane { path.closeSubpath() }
            ctx.stroke(path, with: .color(color.opacity(0.9)),
                       style: StrokeStyle(lineWidth: lineWidth, lineJoin: .round))
        }

        // Point glyphs.
        for (voxel, p) in zip(markup.points, pts) {
            let solid = onSlice(voxel)
            let r: CGFloat = isActive ? 4 : 3
            let rect = CGRect(x: p.x - r, y: p.y - r, width: 2 * r, height: 2 * r)
            if solid {
                ctx.fill(Path(ellipseIn: rect), with: .color(color))
                ctx.stroke(Path(ellipseIn: rect), with: .color(.white.opacity(0.8)),
                           lineWidth: 1)
            } else {
                ctx.stroke(Path(ellipseIn: rect), with: .color(color.opacity(0.7)),
                           lineWidth: 1.2)
            }
        }

        drawMeasurement(markup, pts: pts, color: color, in: &ctx)
    }

    private func drawMeasurement(_ markup: Markup, pts: [CGPoint], color: Color,
                                 in ctx: inout GraphicsContext) {
        let label: String?
        let anchor: CGPoint?
        switch markup.type {
        case .angle:
            label = markups.angleDegrees(of: markup).map { String(format: "%.1f\u{00B0}", $0) }
            anchor = pts.count >= 2 ? pts[1] : pts.first
        case .line, .curve, .closedCurve:
            label = markups.length(of: markup).map { String(format: "%.1f mm", $0) }
            anchor = pts.last
        case .point, .plane:
            label = nil
            anchor = nil
        }
        guard let text = label, let at = anchor else { return }
        ctx.draw(Text(text).font(.caption2.monospacedDigit()).foregroundColor(color),
                 at: CGPoint(x: at.x + 8, y: at.y - 8), anchor: .bottomLeading)
    }
}
